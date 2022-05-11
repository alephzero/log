#pragma once

#include <a0.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "a0/logger/policy.hpp"
#include "a0/logger/unit_parse.hpp"

namespace a0::logger {

static inline std::string replace(std::string str, const std::string& search, const std::string& replace) {
  size_t pos = 0;
  if ((pos = str.find(search, pos)) != std::string::npos) {
    str.replace(pos, search.size(), replace);
    pos += replace.size();
  }
  return str;
}

struct Rule {
  enum Protocol {
    UNKNOWN,
    FILE,
    CFG,
    LOG,
    PRPC,
    PUBSUB,
    RPC,
  };
  Protocol protocol;
  std::string topic;

  std::optional<uint64_t> max_logfile_size;
  std::optional<std::chrono::nanoseconds> max_logfile_duration;

  std::vector<a0::logger::Policy::Config> policies;
  std::string trigger_control_topic;

  std::string relative_watch_path() const {
    static std::map<Protocol, std::string> tmpl_map{
        {Protocol::FILE, "{topic}"},
        {Protocol::CFG, a0_env_topic_tmpl_cfg()},
        {Protocol::LOG, a0_env_topic_tmpl_log()},
        {Protocol::PRPC, a0_env_topic_tmpl_prpc()},
        {Protocol::PUBSUB, a0_env_topic_tmpl_pubsub()},
        {Protocol::RPC, a0_env_topic_tmpl_rpc()},
    };
    return replace(tmpl_map[protocol], "{topic}", topic);
  }

  nlohmann::json self_description;
};

NLOHMANN_JSON_SERIALIZE_ENUM(Rule::Protocol, {
                                                 {Rule::Protocol::UNKNOWN, ""},
                                                 {Rule::Protocol::FILE, "file"},
                                                 {Rule::Protocol::CFG, "cfg"},
                                                 {Rule::Protocol::LOG, "log"},
                                                 {Rule::Protocol::PRPC, "prpc"},
                                                 {Rule::Protocol::PUBSUB, "pubsub"},
                                                 {Rule::Protocol::RPC, "rpc"},
                                             });

static inline void from_json(const nlohmann::json& j, Rule& r) {
  r.self_description = j;

  j.at("protocol").get_to(r.protocol);
  if (r.protocol == Rule::Protocol::UNKNOWN) {
    throw std::invalid_argument("Unknown protocol: " + j.at("protocol").dump());
  }
  j.at("topic").get_to(r.topic);
  j.at("policies").get_to(r.policies);
  if (j.count("max_logfile_size")) {
    r.max_logfile_size = parse_filesize(j.at("max_logfile_size"));
  }
  if (j.count("max_logfile_duration")) {
    r.max_logfile_duration = parse_duration(j.at("max_logfile_duration"));
  }
  if (j.count("trigger_control_topic")) {
    r.trigger_control_topic = j.at("trigger_control_topic");
  }
}

static inline void to_json(nlohmann::json j, const Rule& r) {
  j = r.self_description;
}

}  // namespace a0::logger
