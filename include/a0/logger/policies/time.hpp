#pragma once

#include <a0.h>
#include <chrono>
#include <set>
#include <sstream>

#include "a0/logger/policy.hpp"

// TODO(lshamis): Move this into the main alephzero repo.
namespace a0 {

A0_STATIC_INLINE
TimeMono operator+(TimeMono time_mono, std::chrono::nanoseconds dur) {
  return time_mono.add(dur);
}

A0_STATIC_INLINE
TimeMono operator-(TimeMono time_mono, std::chrono::nanoseconds dur) {
  return time_mono + (-dur);
}

A0_STATIC_INLINE
bool operator<(TimeMono lhs, TimeMono rhs) {
  return lhs.c->ts.tv_sec < rhs.c->ts.tv_sec ||
         (lhs.c->ts.tv_sec == rhs.c->ts.tv_sec && lhs.c->ts.tv_nsec < rhs.c->ts.tv_nsec);
}

A0_STATIC_INLINE
bool operator==(TimeMono lhs, TimeMono rhs) {
  return lhs.c->ts.tv_sec == rhs.c->ts.tv_sec && lhs.c->ts.tv_nsec == rhs.c->ts.tv_nsec;
}

A0_STATIC_INLINE
bool operator!=(TimeMono lhs, TimeMono rhs) { return !(lhs == rhs); }

A0_STATIC_INLINE
bool operator>(TimeMono lhs, TimeMono rhs) { return rhs < lhs; }

A0_STATIC_INLINE
bool operator>=(TimeMono lhs, TimeMono rhs) { return !(lhs < rhs); }

A0_STATIC_INLINE
bool operator<=(TimeMono lhs, TimeMono rhs) { return !(lhs > rhs); }

}  // namespace a0

namespace a0::logger {

class TimePolicy : public Policy::Base {
  std::chrono::nanoseconds save_prev;
  std::chrono::nanoseconds save_next;

  std::deque<TimeMono> trigger_tss;
  std::set<TimeMono> pkt_tss;
  std::map<std::shared_ptr<a0_packet_t>, TimeMono> ts_cache;

  std::chrono::nanoseconds parse_duration(std::string str) {
    double val;
    std::string suffix;
    std::string remainder;

    std::stringstream(std::move(str)) << val << suffix << remainder;
    if (val < 0) {
      throw std::invalid_argument("TimePolicy] Duration parse failed. Value may not be negative");
    }
    if (!remainder.empty()) {
      throw std::invalid_argument("TimePolicy] Duration parse failed. Expect something like 300ms or 2.5s");
    }
    if (!suffix.empty()) {
      throw std::invalid_argument("TimePolicy] Duration parse failed. Missing units");
    }

    if (suffix == "ns") {
    } else if (suffix == "us") {
      val *= 1e3;
    } else if (suffix == "ms") {
      val *= 1e6;
    } else if (suffix == "s") {
      val *= 1e9;
    } else if (suffix == "m") {
      val *= 1e9 * 60;
    } else if (suffix == "h") {
      val *= 1e9 * 60 * 60;
    } else {
      throw std::invalid_argument("TimePolicy] Duration parse failed. Unknown unit " + suffix + ". Known: ns, us, ms, s, m, h");
    }

    return std::chrono::nanoseconds(int64_t(val));
  }

 public:
  // Modifiable for tests.
  std::function<TimeMono()> now{[]() {
    return TimeMono::now();
  }};

  TimePolicy(const nlohmann::json& args) {
    if (!args.count("save_prev") && !args.count("save_next")) {
      throw std::invalid_argument("TimePolicy] Missing at least one of 'save_prev' or 'save_next'");
    }
    if (args.count("save_prev")) {
      save_prev = parse_duration(args["save_prev"].get<std::string>());
    }
    if (args.count("save_next")) {
      save_next = parse_duration(args["save_next"].get<std::string>());
    }
  }

  void onpkt(Packet pkt) override {
    for (auto&& [key, val] : pkt.headers()) {
      if (key == "a0_time_mono") {
        auto time = TimeMono::parse(val);
        pkt_tss.insert(time);
        ts_cache[pkt.c] = time;
        return;
      }
    }
  }

  void ondrop(Packet pkt) override {
    if (ts_cache.count(pkt.c)) {
      pkt_tss.erase(ts_cache[pkt.c]);
      ts_cache.erase(pkt.c);
    }
  }

  void ontrigger() override {
    trigger_tss.push_back(now());
  }

  SaveDecision should_save(Packet pkt) override {
    if (pkt_tss.empty() || !ts_cache.count(pkt.c)) {
      return SaveDecision::DROP;
    }
    // Do some cleanup.
    while (!trigger_tss.empty() && trigger_tss.front() + save_next < *pkt_tss.begin()) {
      trigger_tss.pop_front();
    }

    TimeMono pts = ts_cache[pkt.c];

    // Check if any windows match.
    for (auto& tts : trigger_tss) {
      if (tts - save_prev <= pts && pts <= tts + save_next) {
        return SaveDecision::SAVE;
      }
    }

    // No trigger has marked this message for saving.
    // If a trigger can still save it, defer. Otherwise drop.
    if (now() < pts + save_prev) {
      return SaveDecision::DEFER;
    }
    return SaveDecision::DROP;
  }
};

REGISTER_POLICY(time, TimePolicy);

}  // namespace a0::logger
