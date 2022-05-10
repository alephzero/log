#pragma once

#include <a0.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>

namespace a0::logger {

class Trigger final {
 public:
  struct Config {
    std::string type;
    nlohmann::json args;
    std::string control_topic;
  };

  struct Base {
    virtual ~Base() = default;
  };

  class Controller {
    std::mutex mtx;
    std::vector<std::shared_ptr<bool>> gates;
    Subscriber sub;

   public:
    Controller(std::string topic) : sub(topic, [this, topic](Packet pkt) {
      if (pkt.payload() != "on" && pkt.payload() != "off") {
        fprintf(
            stderr,
            "Invalid trigger control message.\ntopic=[%s]\bmessage=[%s]\nExpect one of {\"on\", \"off\"}\n",
            topic.c_str(),
            pkt.payload().data());
        return;
      }

      bool val = (pkt.payload() == "on");

      std::unique_lock<std::mutex> lk{mtx};
      for (auto& gate : gates) {
        *gate = val;
      }
    }) {}

    static Controller* get(const std::string& topic) {
      static std::mutex mtx;
      static std::unordered_map<std::string, std::unique_ptr<Controller>> global_map;
      std::unique_lock<std::mutex> lk{mtx};
      if (!global_map.count(topic)) {
        global_map[topic] = std::make_unique<Controller>(topic);
      }
      return global_map[topic].get();
    }

    void connect(std::shared_ptr<bool> gate) {
      std::unique_lock<std::mutex> lk{mtx};
      gates.push_back(std::move(gate));
    }
  };

  struct EventCallbacks {
    virtual ~EventCallbacks() = default;
    // ontrigger will be called on separate threads than the other methods.
    virtual void ontrigger() {}
    virtual void onpause() {}
    virtual void onresume() {}
  };

  class ControlledEventCallbacks final {
    std::atomic<bool> enabled{true};
    std::shared_ptr<EventCallbacks> callbacks;

   public:
    ControlledEventCallbacks(std::shared_ptr<EventCallbacks> callbacks) : callbacks{callbacks} {}

    void ontrigger() override {
      if (enabled) {
        callbacks->ontrigger();
      }
    }

    void onpause() override {
      enabled = false;
      callbacks->onpause();
    }

    void onresume() override {
      callbacks->onresume();
      enabled = true;
    }
  };

  using Factory = std::function<std::unique_ptr<Base>(nlohmann::json, std::shared_ptr<EventCallbacks>)>;

  static std::map<std::string, Factory>* registrar() {
    static std::map<std::string, Factory> r;
    return &r;
  }

  static bool register_(std::string key, Factory fact) {
    return registrar()->insert({std::move(key), std::move(fact)}).second;
  }

  Trigger(Config config, std::vector<Controller*> controllers, std::shared_ptr<EventCallbacks> callbacks) {
    if (!registrar()->count(config.type)) {
      throw std::invalid_argument("Unknown trigger: " + config.type);
    }

    auto enabled = std::make_shared<bool>(true);
    for (auto&& controller : controllers) {
      controller->connect(enabled);
    }
    if (!config.control_topic.empty()) {
      Controller::get(config.control_topic)->connect(enabled);
    }

    base = registrar()->at(config.type)(config.args, std::make_shared<ControlledEventCallbacks>(callbacks));
  }

 private:
  std::unique_ptr<Base> base;
  std::vector<std::shared_ptr<Controller>> controllers;
};

A0_STATIC_INLINE
void from_json(const nlohmann::json& j, Trigger::Config& t) {
  j.at("type").get_to(t.type);
  t.args = j.at("args");
  if (j.count("control_topic")) {
    j.at("control_topic").get_to(t.control_topic);
  }
}

}  // namespace a0::logger

#define REGISTER_TRIGGER(key, clz) \
  static bool _##clz##__##key = a0::logger::Trigger::register_(#key, [](nlohmann::json args, a0::logger::Trigger::Notify notify) { return std::make_unique<clz>(args, notify); });
