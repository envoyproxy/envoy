#include "common/upstream/cluster_config_impl.h"

#include <fmt/format.h>

#include "common/common/assert.h"
#include "common/http/utility.h"
#include "common/network/resolver_impl.h"
#include "common/protobuf/utility.h"
#include "common/ssl/context_config_impl.h"

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

namespace {

static Network::Address::InstanceConstSharedPtr
toSourceAddress(const envoy::api::v2::Cluster& config) {
  if (config.upstream_bind_config().has_source_address()) {
    return Network::Address::resolveProtoSocketAddress(
        config.upstream_bind_config().source_address());
  }
  return nullptr;
}

static Ssl::ClientContextConfig::ConstSharedPtr
toTlsContext(const envoy::api::v2::Cluster& config) {
  if (config.has_tls_context()) {
    return std::make_shared<Ssl::ClientContextConfigImpl>(config.tls_context());
  }
  return nullptr;
}

static uint64_t parseFeatures(const envoy::api::v2::Cluster& config) {
  uint64_t features = 0;
  if (config.has_http2_protocol_options()) {
    features |= ClusterInfo::Features::HTTP2;
  }
  return features;
}

static LoadBalancerType toLbType(const envoy::api::v2::Cluster& config) {
  switch (config.lb_policy()) {
    case envoy::api::v2::Cluster::ROUND_ROBIN:
      return LoadBalancerType::RoundRobin;
    case envoy::api::v2::Cluster::LEAST_REQUEST:
      return LoadBalancerType::LeastRequest;
    case envoy::api::v2::Cluster::RANDOM:
      return LoadBalancerType::Random;
    case envoy::api::v2::Cluster::RING_HASH:
      return LoadBalancerType::RingHash;
    case envoy::api::v2::Cluster::ORIGINAL_DST_LB:
      if (config.type() != envoy::api::v2::Cluster::ORIGINAL_DST) {
        throw EnvoyException(fmt::format(
            "cluster: LB type 'original_dst_lb' may only be used with cluser type 'original_dst'"));
      }
      return LoadBalancerType::OriginalDst;
    default:
      NOT_REACHED;
  }
}


static ClusterConfig::OutlierDetectionConfig* toOutlierDetectionConfig(const envoy::api::v2::Cluster& config) {
  if (config.has_outlier_detection()) {
    return new ClusterConfigImpl::OutlierDetectionConfigImpl(config.outlier_detection());
  }
  return nullptr;
}
} // namespace

ClusterConfigImpl::CircuitBreakerConfigImpl::ThresholdsImpl::ThresholdsImpl(const envoy::api::v2::CircuitBreakers_Thresholds& config)
: priority_(config.priority()),
  max_connections_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_connections, 1024)),
  max_pending_requests_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_pending_requests, 1024)),
  max_requests_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_requests, 1024)),
  max_retries_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_retries, 3)) {
}

ClusterConfigImpl::CircuitBreakerConfigImpl::CircuitBreakerConfigImpl(const envoy::api::v2::CircuitBreakers& config)
: thresholds_(static_cast<std::vector<Thresholds::ConstSharedPtr>::size_type>(config.thresholds_size())) {
  const auto& thresholds = config.thresholds();
  for (auto it = thresholds.begin(); it != thresholds.end(); ++it) {
    thresholds_.push_back(std::make_shared<ClusterConfigImpl::CircuitBreakerConfigImpl::ThresholdsImpl>(*it));
  }
}

ClusterConfigImpl::OutlierDetectionConfigImpl::OutlierDetectionConfigImpl(const envoy::api::v2::Cluster::OutlierDetection& config)
    : interval_ms_(static_cast<uint64_t>(PROTOBUF_GET_MS_OR_DEFAULT(config, interval, 10000))),
      base_ejection_time_ms_(
          static_cast<uint64_t>(PROTOBUF_GET_MS_OR_DEFAULT(config, base_ejection_time, 30000))),
      consecutive_5xx_(
          static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, consecutive_5xx, 5))),
      max_ejection_percent_(
          static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_ejection_percent, 10))),
      success_rate_minimum_hosts_(static_cast<uint64_t>(
                                      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, success_rate_minimum_hosts, 5))),
      success_rate_request_volume_(static_cast<uint64_t>(
                                       PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, success_rate_request_volume, 100))),
      success_rate_stdev_factor_(static_cast<uint64_t>(
                                     PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, success_rate_stdev_factor, 1900))),
      enforcing_consecutive_5xx_(static_cast<uint64_t>(
                                     PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enforcing_consecutive_5xx, 100))),
      enforcing_success_rate_(static_cast<uint64_t>(
                                  PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enforcing_success_rate, 100))) {}

ClusterConfigImpl::ClusterConfigImpl(const envoy::api::v2::Cluster& config)
: name_(config.name()),
  max_requests_per_connection_(
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_requests_per_connection, 0)),
  connect_timeout_(
      std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(config, connect_timeout))),
  per_connection_buffer_limit_bytes_(
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
  features_(parseFeatures(config)),
  http2_settings_(Http::Utility::parseHttp2Settings(config.http2_protocol_options())),
  source_address_(toSourceAddress(config)),
  tls_context_config_(toTlsContext(config)),
  lb_type_(toLbType(config)),
  lb_subset_info_(config.lb_subset_config()),
  outlier_detection_config_(toOutlierDetectionConfig(config)),
  circuit_breaker_config_(config.circuit_breakers()) {
  printf("NM: ClusterConfigImpl created\n");
}

} // namespace Upstream
} // namespace Envoy