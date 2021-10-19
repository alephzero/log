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
  struct Match {
    Protocol protocol;
    std::string pattern;
  };
  std::vector<Match> match;

  // std::string search_pattern;
  // a0::PathGlob search_glob;
  std::vector<a0::logger::Policy::Config> policies;

  std::vector<std::string> watch_paths;
  std::vector<a0::PathGlob> glob_cache;
};

bool match(const Rule& r, const std::string& path) {
  for (auto& glob : r.glob_cache) {
    if (glob.match(path)) {
      return true;
    }
  }
  return false;
}

NLOHMANN_JSON_SERIALIZE_ENUM(Rule::Protocol, {
  {Rule::Protocol::FILE, "file"},
  {Rule::Protocol::CFG, "cfg"},
  {Rule::Protocol::LOG, "log"},
  {Rule::Protocol::PRPC, "prpc"},
  {Rule::Protocol::PUBSUB, "pubsub"},
  {Rule::Protocol::RPC, "rpc"},
});


static inline
void from_json(const nlohmann::json& j, Rule::Match& m) {
  j.at("protocol").get_to(m.protocol);
  j.at("pattern").get_to(m.pattern);
}

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
  r.searchpath = "/dev/shm/";
  if (j.count("searchpath")) {
    j.at("searchpath").get_to(r.searchpath);
  }
  j.at("savepath").get_to(r.savepath);
  j.at("match").get_to(r.match);
  j.at("policies").get_to(r.policies);

  // j.at("pattern_type").get_to(r.pattern_type);
  // j.at("pattern").get_to(r.pattern);
  // if (r.pattern[0] == '/') {
  //   throw std::invalid_argument("pattern cannot begin with '/'");
  // }


  // std::string tmpl = std::map<Rule::Protocol, std::string>{
  //   {Rule::Protocol::FILE, "{topic}"},
  //   {Rule::Protocol::CFG, a0_env_topic_tmpl_cfg()},
  //   {Rule::Protocol::LOG, a0_env_topic_tmpl_log()},
  //   {Rule::Protocol::PRPC, a0_env_topic_tmpl_prpc()},
  //   {Rule::Protocol::PUBSUB, a0_env_topic_tmpl_pubsub()},
  //   {Rule::Protocol::RPC, a0_env_topic_tmpl_rpc()},
  // }[r.pattern_type];
  // r.search_pattern = r.searchpath / replace_all(tmpl, "{topic}", r.pattern);

  // r.search_glob = a0::PathGlob(r.search_pattern);

  // j.at("policies").get_to(r.policies);

  std::map<Rule::Protocol, std::string> tmpl_map{
    {Rule::Protocol::FILE, "{topic}"},
    {Rule::Protocol::CFG, a0_env_topic_tmpl_cfg()},
    {Rule::Protocol::LOG, a0_env_topic_tmpl_log()},
    {Rule::Protocol::PRPC, a0_env_topic_tmpl_prpc()},
    {Rule::Protocol::PUBSUB, a0_env_topic_tmpl_pubsub()},
    {Rule::Protocol::RPC, a0_env_topic_tmpl_rpc()},
  };
  for (auto& m : r.match) {
    auto path = r.searchpath / replace_all(tmpl_map[m.protocol], "{topic}", m.pattern);
    r.watch_paths.emplace_back(path);
    r.glob_cache.emplace_back(path);
  }
}

}  // namespace a0::logger
