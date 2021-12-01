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

class Policy final {
 public:
  struct Config {
    std::string type;
    nlohmann::json args;
    std::vector<Trigger::Config> triggers;
  };

  struct Base {
    virtual ~Base() = default;
    virtual void onpkt(Packet) {}
    virtual void ondrop(Packet) {}
    // ontrigger will be called on separate threads than the other methods.
    virtual void ontrigger() {}
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

  Policy(Config config, std::mutex* mtx) {
    base = registrar()->at(config.type)(config.args);

    for (auto&& tcfg : config.triggers) {
      triggers.emplace_back(tcfg, [this, mtx]() {
        std::unique_lock<std::mutex> lk(*mtx);
        base->ontrigger();
      });
    }
  }

  void onpkt(Packet pkt) { base->onpkt(pkt); }
  void ondrop(Packet pkt) { base->ondrop(pkt); }
  void ontrigger() { base->ontrigger(); }
  SaveDecision should_save(Packet pkt) { return base->should_save(pkt); }

 private:
  std::unique_ptr<Base> base;
  std::vector<Trigger> triggers;
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
}

}  // namespace a0::logger

#define REGISTER_POLICY(key, clz) \
  static bool _##clz##__##key = a0::logger::Policy::register_(#key, [](nlohmann::json args) { return std::make_unique<clz>(args); });
