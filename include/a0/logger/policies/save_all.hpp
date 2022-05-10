#pragma once

#include "a0/logger/policy.hpp"

namespace a0::logger {

class SaveAllPolicy : public Policy::Base {
  bool ignore_trigger_control{false};
  std::atomic<bool> enabled{true};

 public:
  SaveAllPolicy(const nlohmann::json& args) {
    if (args.count("ignore_trigger_control")) {
      args["ignore_trigger_control"].get_to(ignore_trigger_control);
    }
  }

  void onpause() {
    if (!ignore_trigger_control) {
      enabled = false;
    }
  }

  void onresume() {
    enabled = true;
  }

  SaveDecision should_save(Packet) {
    return enabled ? SaveDecision::SAVE : SaveDecision::DROP;
  }
};

REGISTER_POLICY(save_all, SaveAllPolicy);

}  // namespace a0::logger
