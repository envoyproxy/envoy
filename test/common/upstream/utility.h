#pragma once

#include "common/common/utility.h"
#include "common/config/cds_json.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Upstream {
namespace {

inline std::string defaultSdsClusterJson(const std::string& name) {
  return fmt::sprintf(R"EOF(
  {
    "name": "%s",
    "connect_timeout_ms": 250,
    "type": "sds",
    "lb_type": "round_robin"
  }
  )EOF",
                      name);
}

inline std::string defaultStaticClusterJson(const std::string& name) {
  return fmt::sprintf(R"EOF(
  {
    "name": "%s",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://127.0.0.1:11001"}]
  }
  )EOF",
                      name);
}

inline std::string clustersJson(const std::vector<std::string>& clusters) {
  return fmt::sprintf("\"clusters\": [%s]", StringUtil::join(clusters, ","));
}

inline envoy::api::v2::Cluster parseClusterFromJson(const std::string& json_string) {
  envoy::api::v2::Cluster cluster;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::CdsJson::translateCluster(*json_object_ptr, Optional<SdsConfig>(), cluster);
  return cluster;
}

inline envoy::api::v2::Cluster defaultStaticCluster(const std::string& name) {
  return parseClusterFromJson(defaultStaticClusterJson(name));
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
