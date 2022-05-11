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

  struct Listener {
    virtual ~Listener() = default;
    // ontrigger will be called on separate threads than the other methods.
    virtual void ontrigger() {}
    virtual void onpause() {}
    virtual void onresume() {}
  };

  class Gate {
   private:
    std::mutex mtx;
    std::vector<Listener*> listeners;
    Subscriber sub;

   public:
    Gate(std::string topic)
        : sub(topic, [this, topic](Packet pkt) {
            if (pkt.payload() != "on" && pkt.payload() != "off") {
              fprintf(
                  stderr,
                  "Invalid trigger control message.\ntopic=[%s]\bmessage=[%s]\nExpect one of {\"on\", \"off\"}\n",
                  topic.c_str(),
                  pkt.payload().data());
              return;
            }

            std::unique_lock<std::mutex> lk{mtx};
            for (auto* l : listeners) {
              if (pkt.payload() == "on") {
                l->onresume();
              } else {
                l->onpause();
              }
            }
          }) {}

    static Gate* get(const std::string& topic) {
      static std::mutex mtx;
      static std::map<std::string, std::unique_ptr<Gate>> global_map;
      std::unique_lock<std::mutex> lk{mtx};
      if (!global_map.count(topic)) {
        global_map[topic] = std::make_unique<Gate>(topic);
      }
      return global_map[topic].get();
    }

    void add_listener(Listener* l) {
      std::unique_lock<std::mutex> lk{mtx};
      listeners.push_back(l);
    }
  };

  using Notify = std::function<void()>;
  using Factory = std::function<std::unique_ptr<Base>(nlohmann::json, Notify)>;

  static std::map<std::string, Factory>* registrar() {
    static std::map<std::string, Factory> r;
    return &r;
  }

  static bool register_(std::string key, Factory fact) {
    return registrar()->insert({std::move(key), std::move(fact)}).second;
  }

  Trigger(Config config, Listener* listener) {
    if (!registrar()->count(config.type)) {
      throw std::invalid_argument("Unknown trigger: " + config.type);
    }

    if (!config.control_topic.empty()) {
      Gate::get(config.control_topic)->add_listener(listener);
    }

    base = registrar()->at(config.type)(config.args, [listener]() { listener->ontrigger(); });
  }

 private:
  std::unique_ptr<Base> base;
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
