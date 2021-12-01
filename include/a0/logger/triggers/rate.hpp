#pragma once

#include <a0.h>

#include <chrono>

#include "a0/logger/trigger.hpp"

namespace a0::logger {

class RateTrigger : public Trigger::Base {
  std::chrono::nanoseconds period;
  std::thread t;
  std::mutex mtx;
  std::condition_variable cv;
  bool running{true};

  void validate_rate(double hz) {
    if (hz < 1.0 / (60 * 60)) {
      throw std::invalid_argument("RateTrigger] hz must be more than once an hour");
    }
    if (hz > 200) {
      throw std::invalid_argument("RateTrigger] hz must be less than 200 times per second");
    }
  }

 public:
  RateTrigger(nlohmann::json args, Trigger::Notify notify) {
    if (!args.count("hz") && !args.count("period")) {
      throw std::invalid_argument("RateTrigger] Missing one of 'hz' or 'period'");
    }
    if (args.count("hz") && args.count("period")) {
      throw std::invalid_argument("RateTrigger] Cannot provide both 'hz' and 'period'");
    }

    if (args.count("period")) {
      period = std::chrono::nanoseconds(uint64_t(1e9 * args["period"].get<double>()));
    } else {
      auto hz = args["hz"].get<double>();
      validate_rate(hz);
      period = std::chrono::nanoseconds(uint64_t(1e9 / hz));
    }

    t = std::thread([this, notify]() {
      std::unique_lock<std::mutex> lk(mtx);
      while (running) {
        lk.unlock();
        notify();
        lk.lock();
        cv.wait_for(lk, period);
      }
    });
  }

  ~RateTrigger() {
    {
      std::unique_lock<std::mutex> lk(mtx);
      running = false;
      cv.notify_all();
    }
    t.join();
  }
};

REGISTER_TRIGGER(rate, RateTrigger);

}  // namespace a0::logger
