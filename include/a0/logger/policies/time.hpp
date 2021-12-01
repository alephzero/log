#pragma once

#include <a0.h>

#include <chrono>
#include <set>
#include <sstream>

#include "a0/logger/policy.hpp"
#include "a0/logger/unit_parse.hpp"

namespace a0::logger {

class TimePolicy : public Policy::Base {
  std::chrono::nanoseconds save_prev;
  std::chrono::nanoseconds save_next;

  std::deque<TimeMono> trigger_tss;
  std::deque<std::pair<Packet, TimeMono>> pkt_tss;

 public:
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
        pkt_tss.push_back({pkt, time});
        return;
      }
    }
  }

  void ondrop(Packet pkt) override {
    if (pkt_tss.front().first.c == pkt.c) {
      pkt_tss.pop_front();
    }
  }

  void ontrigger() override {
    trigger_tss.push_back(TimeMono::now());
  }

  SaveDecision should_save(Packet pkt) override {
    if (pkt_tss.empty() || pkt_tss.front().first.c != pkt.c) {
      return SaveDecision::DROP;
    }

    TimeMono pkt_ts = pkt_tss.front().second;

    // Do some cleanup.
    while (!trigger_tss.empty() && trigger_tss.front() + save_next < pkt_ts) {
      trigger_tss.pop_front();
    }

    // Check if any windows match.
    for (auto& trig_ts : trigger_tss) {
      if (trig_ts - save_prev <= pkt_ts && pkt_ts <= trig_ts + save_next) {
        return SaveDecision::SAVE;
      }
    }

    // No trigger has marked this message for saving.
    // If a trigger can still save it, defer. Otherwise drop.
    if (TimeMono::now() < pkt_ts + save_prev) {
      return SaveDecision::DEFER;
    }
    return SaveDecision::DROP;
  }
};

REGISTER_POLICY(time, TimePolicy);

}  // namespace a0::logger
