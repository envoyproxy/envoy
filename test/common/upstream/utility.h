#pragma once

#include "common/config/cds_json.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Upstream {
namespace {

inline envoy::api::v2::Cluster parseClusterFromJson(const std::string& json_string) {
  envoy::api::v2::Cluster cluster;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::CdsJson::translateCluster(*json_object_ptr, Optional<SdsConfig>(), cluster);
  return cluster;
}

inline envoy::api::v2::Cluster parseSdsClusterFromJson(const std::string& json_string,
                                                       const SdsConfig sds_config) {
  envoy::api::v2::Cluster cluster;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::CdsJson::translateCluster(*json_object_ptr, sds_config, cluster);
  return cluster;
}

} // namespace
} // namespace Upstream
} // namespace Envoy
