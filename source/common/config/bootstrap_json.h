#pragma once

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace Config {

class BootstrapJson {
public:
  /**
   * Translate a v1 JSON cluster manager object to v2 envoy::config::bootstrap::v2::Bootstrap.
   * @param json_cluster_manager source v1 JSON cluster manager object.
   * @param bootstrap destination v2 envoy::config::bootstrap::v2::Bootstrap.
   */
  static void translateClusterManagerBootstrap(const Json::Object& json_cluster_manager,
                                               envoy::config::bootstrap::v2::Bootstrap& bootstrap);

  /**
   * Translate a v1 JSON static config object to v2 envoy::config::bootstrap::v2::Bootstrap.
   * @param json_config source v1 JSON static config object.
   * @param bootstrap destination v2 envoy::config::bootstrap::v2::Bootstrap.
   */
  static void translateBootstrap(const Json::Object& json_config,
                                 envoy::config::bootstrap::v2::Bootstrap& bootstrap);
};

} // namespace Config
} // namespace Envoy
