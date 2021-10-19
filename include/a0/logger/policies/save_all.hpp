#pragma once

#include "a0/logger/policy.hpp"

namespace a0::logger {

class SaveAllPolicy : public Policy::Base {
 public:
  SaveAllPolicy(const nlohmann::json&) {}

  SaveDecision should_save(Packet) {
    return SaveDecision::SAVE;
  }
};

REGISTER_POLICY(save_all, SaveAllPolicy);

}  // namespace a0::logger
