#pragma once

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/cluster/circuit_breaker.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/upstream/cluster_manager.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

class CdsJson {
public:
  /**
   * Translate a v1 JSON ring hash config to envoy::api::v2::Cluster::RingHashLbConfig.
   * @param json_ring_hash_lb_config source v1 JSON ring hash config object.
   * @param ring_hash_lb_config destination v2 envoy::api::v2::Cluster::RingHashLbConfig.
   */
  static void
  translateRingHashLbConfig(const Json::Object& json_ring_hash_lb_config,
                            envoy::api::v2::Cluster::RingHashLbConfig& ring_hash_lb_config);

  /**
   * Translate a v1 JSON health check object to v2 envoy::api::v2::core::HealthCheck.
   * @param json_health_check source v1 JSON health check object.
   * @param health_check destination v2 envoy::api::v2::core::HealthCheck.
   */
  static void translateHealthCheck(const Json::Object& json_health_check,
                                   envoy::api::v2::core::HealthCheck& health_check);

  /**
   * Translate a v1 JSON thresholds object to v2 envoy::api::v2::Thresholds.
   * @param json_thresholds source v1 JSON thresholds object.
   * @param priority priority for thresholds.
   * @param thresholds destination v2 envoy::api::v2::Thresholds.
   */
  static void translateThresholds(const Json::Object& json_thresholds,
                                  const envoy::api::v2::core::RoutingPriority& priority,
                                  envoy::api::v2::cluster::CircuitBreakers::Thresholds& thresholds);

  /**
   * Translate a v1 JSON circuit breakers object to v2 envoy::api::v2::cluster::CircuitBreakers.
   * @param json_circuit_breakers source v1 JSON circuit breakers object.
   * @param circuit_breakers destination v2 envoy::api::v2::cluster::CircuitBreakers.
   */
  static void translateCircuitBreakers(const Json::Object& json_circuit_breakers,
                                       envoy::api::v2::cluster::CircuitBreakers& circuit_breakers);

  /**
   * Translate a v1 JSON outlier detection object to v2 envoy::api::v2::OutlierDetection.
   * @param json_outlier_detection source v1 JSON outlier detection object.
   * @param outlier_detection destination v2 envoy::api::v2::OutlierDetection.
   */
  static void
  translateOutlierDetection(const Json::Object& json_outlier_detection,
                            envoy::api::v2::cluster::OutlierDetection& outlier_detection);

  /**
   * Translate a v1 JSON Cluster to v2 envoy::api::v2::Cluster.
   * @param json_cluster source v1 JSON Cluster object.
   * @param eds_config SDS config if 'sds' discovery type.
   * @param cluster destination v2 envoy::api::v2::Cluster.
   */
  static void translateCluster(const Json::Object& json_cluster,
                               const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config,
                               envoy::api::v2::Cluster& cluster);
};

} // namespace Config
} // namespace Envoy
