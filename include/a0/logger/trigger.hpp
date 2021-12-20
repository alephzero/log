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
  };

  struct Base {
    virtual ~Base() = default;
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

  Trigger(Config config, Notify notify) {
    if (!registrar()->count(config.type)) {
      throw std::invalid_argument("Unknown trigger: " + config.type);
    }
    base = registrar()->at(config.type)(config.args, notify);
  }

 private:
  std::unique_ptr<Base> base;
};

A0_STATIC_INLINE
void from_json(const nlohmann::json& j, Trigger::Config& t) {
  j.at("type").get_to(t.type);
  t.args = j.at("args");
}

}  // namespace a0::logger

#define REGISTER_TRIGGER(key, clz) \
  static bool _##clz##__##key = a0::logger::Trigger::register_(#key, [](nlohmann::json args, a0::logger::Trigger::Notify notify) { return std::make_unique<clz>(args, notify); });
