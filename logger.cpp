#include "a0/logger/rule.hpp"

#include "a0/logger/triggers/cron.hpp"
#include "a0/logger/triggers/pubsub.hpp"
#include "a0/logger/triggers/rate.hpp"

#include "a0/logger/policies/count.hpp"
#include "a0/logger/policies/drop_all.hpp"
#include "a0/logger/policies/save_all.hpp"
#include "a0/logger/policies/time.hpp"

#include <unordered_set>

#include <a0.h>
#include <deque>
#include <signal.h>
#include <vector>
#include <iostream>
#include <unistd.h>

static FILE* dbg_fd;

namespace a0::logger {

static const size_t kMaxLogfileSize = 128 * 1024 * 1024;

class FileLogger {
  Rule rule;
  std::mutex mtx;

  std::deque<Packet> buffer;
  std::vector<Policy> policies;

  File write_file;
  Transport write_transport;
  Writer writer;

  File read_file;
  Reader reader;  // Must be defined last.

public:
  FileLogger(Rule rule, File read_file) : rule{rule}, read_file{read_file} {
    fprintf(dbg_fd, "FileLogger(%s)\n", read_file.path().c_str());
    if (rule.policies.empty()) {
      return;
    }

    for (auto&& policy_cfg : rule.policies) {
      policies.push_back(Policy(policy_cfg, &mtx));
    }

    reader = Reader(read_file, A0_INIT_OLDEST, A0_ITER_NEXT, [this](Packet pkt) {
      fprintf(dbg_fd, "reader callback\n");
      std::unique_lock<std::mutex> lk(mtx);
      onpkt(pkt);
    });
  }

  ~FileLogger() {
    fprintf(dbg_fd, "~FileLogger()\n");
    reader = {};
    close_current_file();
  }

private:
  void onpkt(Packet pkt) {
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
    }
    write_file = File();
  }

  void maybe_start_next_file(Packet pkt) {
    fprintf(dbg_fd, "maybe_start_next_file\n");
    // TODO(lshamis): Should we split on the hour mark?
    if (!write_file.c || write_would_overflow(pkt)) {
      start_next_file(pkt);
    }
  }

  bool write_would_overflow(Packet pkt) {
    a0_packet_stats_t pkt_stats;
    a0_packet_stats(*pkt.c, &pkt_stats);
    return write_transport.lock().alloc_evicts(pkt_stats.serial_size);
  }

  void start_next_file(Packet pkt) {
    fprintf(dbg_fd, "start_next_file\n");
    close_current_file();

    auto rel = std::filesystem::relative(read_file.path(), rule.searchpath);

    auto now = a0::TimeWall::now();
    struct tm now_tm;
    gmtime_r(&now.c->ts.tv_sec, &now_tm);

    char date_str[11];
    strftime(&date_str[0], 11, "%Y/%m/%d", &now_tm);
    date_str[10] = 0;

    std::string dst = rule.savepath / std::string(date_str) / rel;
    dst += "@";
    dst += pkt.headers().find("a0_time_wall")->second;
    dst += ".a0";
    fprintf(dbg_fd, "creating out file: %s\n", dst.c_str());

    auto file_opts = File::Options::DEFAULT;
    file_opts.create_options.size = kMaxLogfileSize;
    file_opts.open_options.arena_mode = A0_ARENA_MODE_EXCLUSIVE;
    write_file = File(dst, file_opts);
    write_transport = Transport(write_file);
    writer = Writer(write_file);
  }
};

class Logger {
  const std::vector<Rule> rules;

  std::mutex mtx;
  std::unordered_set<std::string> seen_filepath;
  std::vector<std::unique_ptr<FileLogger>> file_loggers;
  std::vector<Discovery> watchers;

  void maybe_create_file_logger(const std::string& filepath) {
    for (auto&& rule : rules) {
      if (match(rule, filepath)) {
        file_loggers.push_back(std::make_unique<FileLogger>(rule, a0::File(filepath)));
        return;
      }
    }
  }

public:
  Logger(std::vector<Rule> rules_) : rules{std::move(rules_)} {
    for (auto&& rule : rules) {
      for (auto&& watch_path : rule.watch_paths) {
        fprintf(dbg_fd, "watch_path=%s\n", watch_path.c_str());
        watchers.emplace_back(watch_path, [this](const std::string& filepath) {
          fprintf(dbg_fd, "found filepath=%s\n", filepath.c_str());
          std::unique_lock<std::mutex> lk(mtx);
          if (seen_filepath.insert(filepath).second) {
            maybe_create_file_logger(filepath);
          }
        });
      }
    }
  }
};

}  // namespace a0::logger

static inline
std::string_view env_lookup(std::string_view key, std::string_view default_) {
  const char* val = std::getenv(key.data());
  return val ? val : default_;
}

int main() {
  dbg_fd = fopen("/dev/shm/logger.dbg", "w");
  auto LOGGER_READY_TOPIC = std::string(env_lookup("LOGGER_READY_TOPIC", "logger_ready"));

  a0::Cfg cfg(a0::env::topic());
  auto rules = cfg.var<std::vector<a0::logger::Rule>>("");
  a0::logger::Logger logger(*rules);

  a0::Publisher(LOGGER_READY_TOPIC).pub("ready");

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGHUP);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);

  sigprocmask(SIG_BLOCK, &sigset, NULL);

  int signo;
  fprintf(dbg_fd, "waiting for shutdown signal\n");
  sigwait(&sigset, &signo);
  fprintf(dbg_fd, "got shutdown signal\n");
  fflush(dbg_fd);
  fclose(dbg_fd);
}
