#pragma once

#include <a0.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

#include "a0/logger/policy.hpp"

namespace a0::logger {

static inline
std::string replace_all(std::string str, const std::string& search, const std::string& replace) {
  size_t pos = 0;
  while((pos = str.find(search, pos)) != std::string::npos) {
    str.replace(pos, search.size(), replace);
    pos += replace.size();
  }
  return str;
}

struct Rule {
  enum Protocol {
    FILE,
    CFG,
    LOG,
    PRPC,
    PUBSUB,
    RPC,
  };
  Protocol protocol;
  std::string topic;

  std::vector<a0::logger::Policy::Config> policies;

  std::string relative_watch_path() const {
    static std::map<Protocol, std::string> tmpl_map{
      {Protocol::FILE, "{topic}"},
      {Protocol::CFG, a0_env_topic_tmpl_cfg()},
      {Protocol::LOG, a0_env_topic_tmpl_log()},
      {Protocol::PRPC, a0_env_topic_tmpl_prpc()},
      {Protocol::PUBSUB, a0_env_topic_tmpl_pubsub()},
      {Protocol::RPC, a0_env_topic_tmpl_rpc()},
    };
    return replace_all(tmpl_map[protocol], "{topic}", topic);
  }
};

NLOHMANN_JSON_SERIALIZE_ENUM(Rule::Protocol, {
  {Rule::Protocol::FILE, "file"},
  {Rule::Protocol::CFG, "cfg"},
  {Rule::Protocol::LOG, "log"},
  {Rule::Protocol::PRPC, "prpc"},
  {Rule::Protocol::PUBSUB, "pubsub"},
  {Rule::Protocol::RPC, "rpc"},
});

static inline
void from_json(const nlohmann::json& j, Rule& r) {
  j.at("protocol").get_to(r.protocol);
  j.at("topic").get_to(r.topic);
  j.at("policies").get_to(r.policies);
}

}  // namespace a0::logger
