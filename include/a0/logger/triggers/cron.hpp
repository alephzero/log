#pragma once

#include <croncpp.h>

#include "a0/logger/trigger.hpp"

namespace a0::logger {

class CronTrigger : public Trigger::Base {
  std::thread t;
  std::mutex mtx;
  std::condition_variable cv;
  bool running{true};

 public:
  CronTrigger(nlohmann::json args, Trigger::Notify notify) {
    if (!args.count("pattern")) {
      throw std::invalid_argument("CronTrigger] Missing pattern");
    }
    auto scheduler = cron::make_cron(args["pattern"].get<std::string>());

    t = std::thread([this, notify, scheduler]() {
      std::unique_lock<std::mutex> lk(mtx);
      while (running) {
        auto now_cpp = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now_cpp);

        lk.unlock();
        notify();
        lk.lock();

        auto next_wake_c = cron::cron_next(scheduler, now_c);
        auto next_wake_cpp = std::chrono::system_clock::from_time_t(next_wake_c);

        cv.wait_until(lk, next_wake_cpp);
      }
    });
  }

  ~CronTrigger() {
    {
      std::unique_lock<std::mutex> lk(mtx);
      running = false;
      cv.notify_all();
    }
    t.join();
  }
};

REGISTER_TRIGGER(cron, CronTrigger);

}  // namespace a0::logger
