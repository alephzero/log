#pragma once

#include "a0/logger/policy.hpp"

namespace a0::logger {

class DropAllPolicy : public Policy::Base {
 public:
  DropAllPolicy(const nlohmann::json&) {}

  SaveDecision should_save(Packet) override {
    return SaveDecision::DROP;
  }
};

REGISTER_POLICY(drop_all, DropAllPolicy);

}  // namespace a0::logger
