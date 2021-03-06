#pragma once

#include <a0.h>

#include "a0/logger/trigger.hpp"

namespace a0::logger {

class PubsubTrigger : public Trigger::Base {
  Subscriber sub;

 public:
  PubsubTrigger(nlohmann::json args, Trigger::Notify notify) {
    if (!args.count("topic")) {
      throw std::invalid_argument("PubsubTrigger] Missing topic");
    }

    sub = Subscriber(
        args["topic"].get<std::string>(),
        ITER_NEWEST,
        [notify](Packet) { notify(); });
  }
};

REGISTER_TRIGGER(pubsub, PubsubTrigger);

}  // namespace a0::logger
