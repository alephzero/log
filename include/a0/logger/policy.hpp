#pragma once

#include <a0.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>

#include "a0/logger/trigger.hpp"

namespace a0::logger {

enum class SaveDecision {
  SAVE,
  DROP,
  DEFER,
};

class Policy final : public Trigger::Listener {
 public:
  struct Config {
    std::string type;
    nlohmann::json args;
    std::vector<Trigger::Config> triggers;
    std::string trigger_control_topic;
  };

  struct Base : Trigger::Listener {
    virtual ~Base() = default;
    virtual void onpkt(Packet) {}
    virtual void ondrop(Packet) {}
    virtual SaveDecision should_save(Packet) = 0;
  };

  using Factory = std::function<std::unique_ptr<Base>(nlohmann::json)>;

  static std::map<std::string, Factory>* registrar() {
    static std::map<std::string, Factory> r;
    return &r;
  }

  static bool register_(std::string key, Factory fact) {
    return registrar()->insert({std::move(key), std::move(fact)}).second;
  }

  Policy(Config config,
         std::mutex* mtx_,
         std::vector<std::string> trigger_control_topics)
      : mtx{mtx_} {
    if (!registrar()->count(config.type)) {
      throw std::invalid_argument("Unknown policy: " + config.type);
    }

    if (!config.trigger_control_topic.empty()) {
      trigger_control_topics.push_back(config.trigger_control_topic);
    }
    for (auto&& tcfg : config.triggers) {
      if (!tcfg.control_topic.empty()) {
        trigger_control_topics.push_back(tcfg.control_topic);
      }
    }

    for (auto&& trigger_control_topic : trigger_control_topics) {
      Trigger::Gate::get(trigger_control_topic)->add_listener(this);
    }

    base = registrar()->at(config.type)(config.args);
    if (trigger_control_topics.empty()) {
      onresume();
    } else {
      onpause();
    }

    for (auto&& tcfg : config.triggers) {
      triggers.emplace_back(tcfg, this);
    }
  }

  void onpkt(Packet pkt) { base->onpkt(pkt); }
  void ondrop(Packet pkt) { base->ondrop(pkt); }
  void ontrigger() override {
    std::unique_lock<std::mutex> lk{*mtx};
    if (triggers_enabled) {
      base->ontrigger();
    }
  }
  void onpause() override {
    std::unique_lock<std::mutex> lk{*mtx};
    base->onpause();
    triggers_enabled = false;
  }
  void onresume() override {
    std::unique_lock<std::mutex> lk{*mtx};
    triggers_enabled = true;
    base->onresume();
  }
  SaveDecision should_save(Packet pkt) { return base->should_save(pkt); }

 private:
  std::mutex* mtx;
  std::unique_ptr<Base> base;
  std::vector<Trigger> triggers;
  bool triggers_enabled{true};
};

A0_STATIC_INLINE
void from_json(const nlohmann::json& j, Policy::Config& t) {
  j.at("type").get_to(t.type);
  if (j.count("args")) {
    t.args = j.at("args");
  }
  if (j.count("triggers")) {
    j.at("triggers").get_to(t.triggers);
  }
  if (j.count("trigger_control_topic")) {
    j.at("trigger_control_topic").get_to(t.trigger_control_topic);
  }
}

}  // namespace a0::logger

#define REGISTER_POLICY(key, clz) \
  static bool _##clz##__##key = a0::logger::Policy::register_(#key, [](nlohmann::json args) { return std::make_unique<clz>(args); });
