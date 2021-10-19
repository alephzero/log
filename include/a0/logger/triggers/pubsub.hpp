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

    fprintf(stderr, "qaz pubsub trigger topic=%s\n", args["topic"].get<std::string>().c_str());
    sub = Subscriber(
        args["topic"].get<std::string>(),
        A0_INIT_AWAIT_NEW,
        A0_ITER_NEWEST,
        [notify](Packet) { fprintf(stderr, "qaz pubsub trigger notify\n"); notify(); });
  }
};

REGISTER_TRIGGER(pubsub, PubsubTrigger);

}  // namespace a0::logger
