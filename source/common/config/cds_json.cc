#include "common/config/cds_json.h"

#include "common/common/assert.h"
#include "common/config/address_json.h"
#include "common/config/json_utility.h"
#include "common/config/protocol_json.h"
#include "common/config/tls_context_json.h"
#include "common/config/utility.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Config {

void CdsJson::translateRingHashLbConfig(
    const Json::Object& json_ring_hash_lb_config,
    envoy::api::v2::Cluster::RingHashLbConfig& ring_hash_lb_config) {
  JSON_UTIL_SET_INTEGER(json_ring_hash_lb_config, ring_hash_lb_config, minimum_ring_size);
  JSON_UTIL_SET_BOOL(json_ring_hash_lb_config, *ring_hash_lb_config.mutable_deprecated_v1(),
                     use_std_hash);
}

void CdsJson::translateHealthCheck(const Json::Object& json_health_check,
                                   envoy::api::v2::core::HealthCheck& health_check) {
  json_health_check.validateSchema(Json::Schema::CLUSTER_HEALTH_CHECK_SCHEMA);

  JSON_UTIL_SET_DURATION(json_health_check, health_check, timeout);
  JSON_UTIL_SET_DURATION(json_health_check, health_check, interval);
  JSON_UTIL_SET_DURATION(json_health_check, health_check, interval_jitter);
  JSON_UTIL_SET_INTEGER(json_health_check, health_check, unhealthy_threshold);
  JSON_UTIL_SET_INTEGER(json_health_check, health_check, healthy_threshold);
  JSON_UTIL_SET_BOOL(json_health_check, health_check, reuse_connection);

  const std::string hc_type = json_health_check.getString("type");
  if (hc_type == "http") {
    health_check.mutable_http_health_check()->set_path(json_health_check.getString("path"));
    if (json_health_check.hasObject("service_name")) {
      health_check.mutable_http_health_check()->set_service_name(
          json_health_check.getString("service_name"));
    }
  } else if (hc_type == "tcp") {
    auto* tcp_health_check = health_check.mutable_tcp_health_check();
    std::string send_text;
    for (const Json::ObjectSharedPtr& entry : json_health_check.getObjectArray("send")) {
      const std::string hex_string = entry->getString("binary");
      send_text += hex_string;
    }
    if (!send_text.empty()) {
      tcp_health_check->mutable_send()->set_text(send_text);
    }
    for (const Json::ObjectSharedPtr& entry : json_health_check.getObjectArray("receive")) {
      const std::string hex_string = entry->getString("binary");
      tcp_health_check->mutable_receive()->Add()->set_text(hex_string);
    }
  } else {
    ASSERT(hc_type == "redis");
    health_check.mutable_redis_health_check();
  }
}

void CdsJson::translateThresholds(
    const Json::Object& json_thresholds, const envoy::api::v2::core::RoutingPriority& priority,
    envoy::api::v2::cluster::CircuitBreakers::Thresholds& thresholds) {
  thresholds.set_priority(priority);
  JSON_UTIL_SET_INTEGER(json_thresholds, thresholds, max_connections);
  JSON_UTIL_SET_INTEGER(json_thresholds, thresholds, max_pending_requests);
  JSON_UTIL_SET_INTEGER(json_thresholds, thresholds, max_requests);
  JSON_UTIL_SET_INTEGER(json_thresholds, thresholds, max_retries);
}

void CdsJson::translateCircuitBreakers(const Json::Object& json_circuit_breakers,
                                       envoy::api::v2::cluster::CircuitBreakers& circuit_breakers) {
  translateThresholds(*json_circuit_breakers.getObject("default", true),
                      envoy::api::v2::core::RoutingPriority::DEFAULT,
                      *circuit_breakers.mutable_thresholds()->Add());
  translateThresholds(*json_circuit_breakers.getObject("high", true),
                      envoy::api::v2::core::RoutingPriority::HIGH,
                      *circuit_breakers.mutable_thresholds()->Add());
}

void CdsJson::translateOutlierDetection(
    const Json::Object& json_outlier_detection,
    envoy::api::v2::cluster::OutlierDetection& outlier_detection) {
  JSON_UTIL_SET_DURATION(json_outlier_detection, outlier_detection, interval);
  JSON_UTIL_SET_DURATION(json_outlier_detection, outlier_detection, base_ejection_time);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, consecutive_5xx);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, consecutive_gateway_failure);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, max_ejection_percent);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, enforcing_consecutive_5xx);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection,
                        enforcing_consecutive_gateway_failure);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, enforcing_success_rate);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, success_rate_minimum_hosts);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, success_rate_request_volume);
  JSON_UTIL_SET_INTEGER(json_outlier_detection, outlier_detection, success_rate_stdev_factor);
}

void CdsJson::translateCluster(const Json::Object& json_cluster,
                               const Optional<envoy::api::v2::core::ConfigSource>& eds_config,
                               envoy::api::v2::Cluster& cluster) {
  json_cluster.validateSchema(Json::Schema::CLUSTER_SCHEMA);

  const std::string name = json_cluster.getString("name");
  Utility::checkObjNameLength("Invalid cluster name", name);
  cluster.set_name(name);

  const std::string string_type = json_cluster.getString("type");
  auto set_dns_hosts = [&json_cluster, &cluster] {
    const auto hosts = json_cluster.getObjectArray("hosts");
    std::transform(hosts.cbegin(), hosts.cend(),
                   Protobuf::RepeatedPtrFieldBackInserter(cluster.mutable_hosts()),
                   [](const Json::ObjectSharedPtr& host) {
                     envoy::api::v2::core::Address address;
                     AddressJson::translateAddress(host->getString("url"), true, false, address);
                     return address;
                   });
  };
  if (string_type == "static") {
    cluster.set_type(envoy::api::v2::Cluster::STATIC);
    const auto hosts = json_cluster.getObjectArray("hosts");
    std::transform(hosts.cbegin(), hosts.cend(),
                   Protobuf::RepeatedPtrFieldBackInserter(cluster.mutable_hosts()),
                   [](const Json::ObjectSharedPtr& host) {
                     envoy::api::v2::core::Address address;
                     AddressJson::translateAddress(host->getString("url"), true, true, address);
                     return address;
                   });
  } else if (string_type == "strict_dns") {
    cluster.set_type(envoy::api::v2::Cluster::STRICT_DNS);
    set_dns_hosts();
  } else if (string_type == "logical_dns") {
    cluster.set_type(envoy::api::v2::Cluster::LOGICAL_DNS);
    set_dns_hosts();
  } else if (string_type == "original_dst") {
    if (json_cluster.hasObject("hosts")) {
      throw EnvoyException("original_dst clusters must have no hosts configured");
    }
    cluster.set_type(envoy::api::v2::Cluster::ORIGINAL_DST);
  } else {
    ASSERT(string_type == "sds");
    cluster.set_type(envoy::api::v2::Cluster::EDS);
    cluster.mutable_eds_cluster_config()->mutable_eds_config()->CopyFrom(eds_config.value());
    JSON_UTIL_SET_STRING(json_cluster, *cluster.mutable_eds_cluster_config(), service_name);
  }

  JSON_UTIL_SET_DURATION(json_cluster, cluster, cleanup_interval);
  JSON_UTIL_SET_DURATION(json_cluster, cluster, connect_timeout);
  JSON_UTIL_SET_INTEGER(json_cluster, cluster, per_connection_buffer_limit_bytes);

  const std::string lb_type = json_cluster.getString("lb_type");
  if (lb_type == "round_robin") {
    cluster.set_lb_policy(envoy::api::v2::Cluster::ROUND_ROBIN);
  } else if (lb_type == "least_request") {
    cluster.set_lb_policy(envoy::api::v2::Cluster::LEAST_REQUEST);
  } else if (lb_type == "random") {
    cluster.set_lb_policy(envoy::api::v2::Cluster::RANDOM);
  } else if (lb_type == "original_dst_lb") {
    cluster.set_lb_policy(envoy::api::v2::Cluster::ORIGINAL_DST_LB);
  } else {
    ASSERT(lb_type == "ring_hash");
    cluster.set_lb_policy(envoy::api::v2::Cluster::RING_HASH);
  }

  if (json_cluster.hasObject("ring_hash_lb_config")) {
    translateRingHashLbConfig(*json_cluster.getObject("ring_hash_lb_config"),
                              *cluster.mutable_ring_hash_lb_config());
  }

  if (json_cluster.hasObject("health_check")) {
    translateHealthCheck(*json_cluster.getObject("health_check"),
                         *cluster.mutable_health_checks()->Add());
  }

  JSON_UTIL_SET_INTEGER(json_cluster, cluster, max_requests_per_connection);
  if (json_cluster.hasObject("circuit_breakers")) {
    translateCircuitBreakers(*json_cluster.getObject("circuit_breakers"),
                             *cluster.mutable_circuit_breakers());
  }

  if (json_cluster.hasObject("ssl_context")) {
    TlsContextJson::translateUpstreamTlsContext(*json_cluster.getObject("ssl_context"),
                                                *cluster.mutable_tls_context());
  }

  if (json_cluster.getString("features", "") == "http2" ||
      json_cluster.hasObject("http2_settings")) {
    ProtocolJson::translateHttp2ProtocolOptions(*json_cluster.getObject("http2_settings", true),
                                                *cluster.mutable_http2_protocol_options());
  }

  JSON_UTIL_SET_DURATION(json_cluster, cluster, dns_refresh_rate);
  const std::string dns_lookup_family = json_cluster.getString("dns_lookup_family", "v4_only");
  if (dns_lookup_family == "auto") {
    cluster.set_dns_lookup_family(envoy::api::v2::Cluster::AUTO);
  } else if (dns_lookup_family == "v6_only") {
    cluster.set_dns_lookup_family(envoy::api::v2::Cluster::V6_ONLY);
  } else {
    ASSERT(dns_lookup_family == "v4_only");
    cluster.set_dns_lookup_family(envoy::api::v2::Cluster::V4_ONLY);
  }
  if (json_cluster.hasObject("dns_resolvers")) {
    const auto dns_resolvers = json_cluster.getStringArray("dns_resolvers");
    std::transform(dns_resolvers.cbegin(), dns_resolvers.cend(),
                   Protobuf::RepeatedPtrFieldBackInserter(cluster.mutable_dns_resolvers()),
                   [](const std::string& json_address) {
                     envoy::api::v2::core::Address address;
                     AddressJson::translateAddress(json_address, false, true, address);
                     return address;
                   });
  }

  if (json_cluster.hasObject("outlier_detection")) {
    translateOutlierDetection(*json_cluster.getObject("outlier_detection"),
                              *cluster.mutable_outlier_detection());
  }
}

} // namespace Config
} // namespace Envoy
