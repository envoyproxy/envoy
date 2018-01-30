#pragma once

#include "common/config/lds_json.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Server {
namespace {

inline envoy::api::v2::Listener parseListenerFromJson(const std::string& json_string) {
  envoy::api::v2::Listener listener;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::LdsJson::translateListener(*json_object_ptr, listener);
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
