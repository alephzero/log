#pragma once

#include <a0.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

#include "a0/logger/policy.hpp"

namespace a0::logger {

struct Rule {
  std::filesystem::path searchpath;
  std::filesystem::path savepath;

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

  std::string watch_path;
  a0::PathGlob glob_cache;

  bool match(const std::string& path) const {
    return glob_cache.match(path);
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
std::string replace_all(std::string str, const std::string& search, const std::string& replace) {
  size_t pos = 0;
  while((pos = str.find(search, pos)) != std::string::npos) {
    str.replace(pos, search.size(), replace);
    pos += replace.size();
  }
  return str;
}

static inline
void from_json(const nlohmann::json& j, Rule& r) {
  r.searchpath = a0::env::root();
  if (j.count("searchpath")) {
    j.at("searchpath").get_to(r.searchpath);
  }
  j.at("savepath").get_to(r.savepath);
  j.at("protocol").get_to(r.protocol);
  j.at("topic").get_to(r.topic);
  j.at("policies").get_to(r.policies);

  std::map<Rule::Protocol, std::string> tmpl_map{
    {Rule::Protocol::FILE, "{topic}"},
    {Rule::Protocol::CFG, a0_env_topic_tmpl_cfg()},
    {Rule::Protocol::LOG, a0_env_topic_tmpl_log()},
    {Rule::Protocol::PRPC, a0_env_topic_tmpl_prpc()},
    {Rule::Protocol::PUBSUB, a0_env_topic_tmpl_pubsub()},
    {Rule::Protocol::RPC, a0_env_topic_tmpl_rpc()},
  };

  r.watch_path = r.searchpath / replace_all(tmpl_map[r.protocol], "{topic}", r.topic);
  r.glob_cache = a0::PathGlob(r.watch_path);
}

}  // namespace a0::logger
