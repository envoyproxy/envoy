#pragma once

#include "common/config/lds_json.h"
#include "common/json/json_loader.h"
#include "common/stats/stats_options_impl.h"

namespace Envoy {
namespace Server {
namespace {

inline envoy::api::v2::Listener parseListenerFromJson(const std::string& json_string) {
  envoy::api::v2::Listener listener;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Stats::StatsOptionsImpl stats_options;
  Config::LdsJson::translateListener(*json_object_ptr, listener, stats_options);
  return listener;
}

inline envoy::api::v2::Listener parseListenerFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::Listener listener;
  MessageUtil::loadFromYaml(yaml, listener);
  return listener;
}

} // namespace
} // namespace Server
} // namespace Envoy
