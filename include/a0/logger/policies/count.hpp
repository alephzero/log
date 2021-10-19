#pragma once

#include <deque>
#include <memory>
#include <unordered_set>

#include "a0/logger/policy.hpp"

namespace a0::logger {

class CountPolicy : public Policy::Base {
  int save_prev{0};
  int save_next{0};
  int cur_next_size{0};

  std::deque<std::shared_ptr<a0_packet_t>> history;
  std::unordered_set<std::shared_ptr<a0_packet_t>> to_save;

 public:
  CountPolicy(const nlohmann::json& args) {
    fprintf(stderr, "count.policy.make()\n");
    if (!args.count("save_prev") && !args.count("save_next")) {
      throw std::invalid_argument("CountPolicy] Missing at least one of 'save_prev' or 'save_next'");
    }
    if (args.count("save_prev")) {
      args["save_prev"].get_to(save_prev);
    }
    if (args.count("save_next")) {
      args["save_next"].get_to(save_next);
    }
  }

  void onpkt(Packet pkt) override {
    if (cur_next_size > 0) {
      to_save.insert(pkt.c);
      cur_next_size--;
    }
    history.push_back(pkt.c);
    if (history.size() > save_prev) {
      history.pop_front();
    }
  }

  void ondrop(Packet pkt) override {
    to_save.erase(pkt.c);
  }

  void ontrigger() override {
    fprintf(stderr, "count.ontrigger()\n");
    cur_next_size = save_next;
    to_save.insert(history.begin(), history.end());
  }

  SaveDecision should_save(Packet pkt) override {
    return to_save.count(pkt.c) ? SaveDecision::SAVE : SaveDecision::DROP;
  }
};

REGISTER_POLICY(count, CountPolicy);

}  // namespace a0::logger
