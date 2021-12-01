#include <a0.h>
#include <signal.h>
#include <unistd.h>

#include <deque>
#include <unordered_set>
#include <vector>

#include "a0/logger/policies/count.hpp"
#include "a0/logger/policies/drop_all.hpp"
#include "a0/logger/policies/save_all.hpp"
#include "a0/logger/policies/time.hpp"
#include "a0/logger/rule.hpp"
#include "a0/logger/triggers/cron.hpp"
#include "a0/logger/triggers/pubsub.hpp"
#include "a0/logger/triggers/rate.hpp"

namespace a0::logger {

static const uint64_t kDefaultMaxLogfileSize = 128 * 1024 * 1024;
static const std::chrono::nanoseconds kDefaultMaxLogfileDuration = std::chrono::hours(1);

struct Config {
  std::filesystem::path searchpath;
  std::filesystem::path savepath;
  std::vector<Rule> rules;
  uint64_t default_max_logfile_size;
  std::chrono::nanoseconds default_max_logfile_duration;
};

static inline void from_json(const nlohmann::json& j, Config& c) {
  c.searchpath = env::root();
  if (j.count("searchpath")) {
    j.at("searchpath").get_to(c.searchpath);
  }
  j.at("savepath").get_to(c.savepath);
  j.at("rules").get_to(c.rules);

  c.default_max_logfile_size = kDefaultMaxLogfileSize;
  if (j.count("default_max_logfile_size")) {
    c.default_max_logfile_size = parse_filesize(j.at("default_max_logfile_size"));
  }
  c.default_max_logfile_duration = kDefaultMaxLogfileDuration;
  if (j.count("default_max_logfile_duration")) {
    c.default_max_logfile_duration = parse_duration(j.at("default_max_logfile_duration"));
  }
}

void announce(const nlohmann::json& j) {
  static Publisher p(std::string(env::topic()) + "/announce");
  p.pub(j.dump());
}

class FileLogger {
  const Config config;
  const Rule rule;
  std::mutex mtx;

  std::deque<Packet> buffer;
  std::vector<Policy> policies;

  File write_file;
  TimeMono write_file_start;
  Transport write_transport;
  Writer writer;

  File read_file;
  Reader reader;  // Must be defined last.

 public:
  FileLogger(Config config, Rule rule, File read_file)
      : config{config}, rule{rule}, read_file{read_file} {
    if (rule.policies.empty()) {
      return;
    }

    for (auto&& policy_cfg : rule.policies) {
      policies.emplace_back(policy_cfg, &mtx);
    }

    // TODO(lshamis): This is likely to pick up old messages, from old runs.
    reader = Reader(read_file, A0_INIT_OLDEST, A0_ITER_NEXT, [this](Packet pkt) {
      if (!has_stamp(pkt)) {
        // TODO(lshamis): Let someone know?
        return;
      }
      std::unique_lock<std::mutex> lk(mtx);
      onpkt(pkt);
    });
  }

  ~FileLogger() {
    reader = {};

    while (!buffer.empty()) {
      auto pkt = buffer.front();
      buffer.pop_front();

      if (should_save(pkt) == SaveDecision::SAVE) {
        maybe_start_next_file(pkt);
        writer.write(pkt);
      }
      for (auto&& p : policies) {
        p.ondrop(pkt);
      }
    }

    close_current_file();
  }

 private:
  void announce_action(std::string action) {
    announce({
        {"action", std::move(action)},
        {"write_file", write_file.path()},
        {"read_file", read_file.path()},
        {"relative_path", std::string(std::filesystem::relative(read_file.path(), config.searchpath))},
        {"rule", rule.self_description},
    });
  }

  void onpkt(Packet pkt) {
    for (auto&& p : policies) {
      p.onpkt(pkt);
    }

    buffer.push_back(pkt);

    while (!buffer.empty()) {
      switch (should_save(buffer.front())) {
        case SaveDecision::SAVE: {
          maybe_start_next_file(buffer.front());
          writer.write(buffer.front());
          [[fallthrough]];
        };
        case SaveDecision::DROP: {
          for (auto&& p : policies) {
            p.ondrop(buffer.front());
          }
          buffer.pop_front();
          break;
        };
        case SaveDecision::DEFER: {
          return;
        };
      }
    }
  }

  SaveDecision should_save(Packet pkt) {
    SaveDecision sd = SaveDecision::DROP;
    for (auto&& p : policies) {
      auto pd = p.should_save(pkt);
      if (pd == SaveDecision::SAVE) {
        return SaveDecision::SAVE;
      } else if (pd == SaveDecision::DEFER) {
        sd = SaveDecision::DEFER;
      }
    }
    return sd;
  }

  void close_current_file() {
    if (write_file.c) {
      auto tlk = write_transport.lock();
      tlk.resize(tlk.used_space());
      std::filesystem::resize_file(write_file.path(), tlk.used_space());
      announce_action("closed");
    }
    write_file = File();
  }

  void maybe_start_next_file(Packet pkt) {
    if (!write_file.c || write_would_exceed_size(pkt) || write_would_exceed_duration(pkt)) {
      start_next_file(pkt);
      announce_action("opened");
    }
  }

  bool write_would_exceed_size(Packet pkt) {
    a0_packet_stats_t pkt_stats;
    a0_packet_stats(*pkt.c, &pkt_stats);
    return write_transport.lock().alloc_evicts(pkt_stats.serial_size);
  }

  bool write_would_exceed_duration(Packet pkt) {
    return write_file_start + max_file_dur() < monotime_from(pkt);
  }

  uint64_t max_file_size() {
    if (rule.max_logfile_size) {
      return *rule.max_logfile_size;
    }
    return config.default_max_logfile_size;
  }

  std::chrono::nanoseconds max_file_dur() {
    if (rule.max_logfile_duration) {
      return *rule.max_logfile_duration;
    }
    return config.default_max_logfile_duration;
  }

  bool has_stamp(Packet pkt) {
    return pkt.headers().find("a0_time_mono") != pkt.headers().end() &&
           pkt.headers().find("a0_time_wall") != pkt.headers().end();
  }

  TimeMono monotime_from(Packet pkt) {
    return TimeMono::parse(pkt.headers().find("a0_time_mono")->second);
  }

  TimeWall walltime_from(Packet pkt) {
    return TimeWall::parse(pkt.headers().find("a0_time_wall")->second);
  }

  void start_next_file(Packet pkt) {
    close_current_file();

    auto rel = std::filesystem::relative(read_file.path(), config.searchpath);

    auto walltime = walltime_from(pkt);

    struct tm now_tm;
    gmtime_r(&walltime.c->ts.tv_sec, &now_tm);

    char date_str[11];
    strftime(&date_str[0], 11, "%Y/%m/%d", &now_tm);
    date_str[10] = 0;

    std::string dst = config.savepath / std::string(date_str) / rel;
    dst += "@";
    dst += walltime.to_string();
    dst += ".a0";

    auto file_opts = File::Options::DEFAULT;
    file_opts.create_options.size = max_file_size();
    file_opts.open_options.arena_mode = A0_ARENA_MODE_EXCLUSIVE;
    // TODO(lshamis): Check whether the file already exists.
    write_file = File(dst, file_opts);
    write_file_start = monotime_from(pkt);
    write_transport = Transport(write_file);
    writer = Writer(write_file);
  }
};

class Logger {
  const Config config;

  std::mutex mtx;
  std::unordered_set<std::string> seen_filepath;
  std::vector<std::unique_ptr<FileLogger>> file_loggers;
  std::vector<Discovery> watchers;

  void maybe_create_file_logger(const std::string& filepath) {
    for (auto&& rule : config.rules) {
      auto path_glob = a0::PathGlob(config.searchpath / rule.relative_watch_path());
      if (path_glob.match(filepath)) {
        file_loggers.push_back(std::make_unique<FileLogger>(config, rule, a0::File(filepath)));
        return;
      }
    }
  }

 public:
  Logger(Config config_)
      : config{std::move(config_)} {
    for (auto&& rule : config.rules) {
      auto watch_path = config.searchpath / rule.relative_watch_path();
      watchers.emplace_back(watch_path, [this](const std::string& filepath) {
        std::unique_lock<std::mutex> lk(mtx);
        if (seen_filepath.insert(filepath).second) {
          maybe_create_file_logger(filepath);
        }
      });
    }
  }
};

}  // namespace a0::logger

int main() {
  a0::Cfg cfg(a0::env::topic());
  auto config = cfg.var<a0::logger::Config>("");
  a0::logger::Logger logger(*config);

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGHUP);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);

  sigprocmask(SIG_BLOCK, &sigset, NULL);

  int signo;
  sigwait(&sigset, &signo);
}
