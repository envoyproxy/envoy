#pragma once

#include "envoy/json/json_object.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace Config {

class BootstrapJson {
public:
  /**
   * Translate a v1 JSON cluster manager object to v2 envoy::api::v2::Bootstrap.
   * @param json_cluster_manager source v1 JSON cluster manager object.
   * @param bootstrap destination v2 envoy::api::v2::Bootstrap.
   */
  static void translateClusterManagerBootstrap(const Json::Object& json_cluster_manager,
                                               envoy::api::v2::Bootstrap& bootstrap);

  /**
   * Translate a v1 JSON static config object to v2 envoy::api::v2::Bootstrap.
   * @param json_config source v1 JSON static config object.
   * @param bootstrap destination v2 envoy::api::v2::Bootstrap.
   */
  static void translateBootstrap(const Json::Object& json_config,
                                 envoy::api::v2::Bootstrap& bootstrap);
};

} // namespace Config
} // namespace Envoy
