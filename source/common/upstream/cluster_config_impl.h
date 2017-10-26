#pragma once

#include <chrono>
#include <string>

#include "common/upstream/load_balancer_impl.h"

#include "envoy/upstream/cluster_config.h"
#include "envoy/network/address.h"
#include "envoy/upstream/load_balancer_type.h"

#include "external/envoy_api/api/cds.pb.h"

namespace Envoy {
namespace Upstream {

class ClusterConfigImpl : public ClusterConfig {
public:

  ClusterConfigImpl(const envoy::api::v2::Cluster& cluster);
  virtual ~ClusterConfigImpl() {}

  const std::string& name() const override {
    return name_;
  }

  uint64_t maxRequestsPerConnection() const override {
    return max_requests_per_connection_;
  }

  const std::chrono::milliseconds& connectTimeout() const override {
    return connect_timeout_;
  }

  uint32_t perConnectionBufferLimitBytes() const override {
    return per_connection_buffer_limit_bytes_;
  }

  uint64_t features() const override {
    return features_;
  }

  const Http::Http2Settings& http2Settings() const override {
    return http2_settings_;
  }

  const Network::Address::InstanceConstSharedPtr& sourceAddress() const override {
    return source_address_;
  }

  const Ssl::ClientContextConfig::ConstSharedPtr& tlsContextConfig() const override {
    return tls_context_config_;
  }

  LoadBalancerType lbType() const override {
    return lb_type_;
  }

  const LoadBalancerSubsetInfoImpl& lbSubsetInfo() const override {
    return lb_subset_info_;
  }

  const OutlierDetectionConfig::ConstSharedPtr& outlierDetectionConfig() const override {
    return outlier_detection_config_;
  }

  const CircuitBreakerConfig& circuitBreakerConfig() const override {
    return circuit_breaker_config_;
  }

public:

  /**
   * Configuration for the outlier detection.
   */
  class OutlierDetectionConfigImpl : public OutlierDetectionConfig {
   public:
    OutlierDetectionConfigImpl(const envoy::api::v2::Cluster::OutlierDetection& config);
    virtual ~OutlierDetectionConfigImpl() {}

    uint64_t intervalMs() const override { return interval_ms_; }
    uint64_t baseEjectionTimeMs() const override { return base_ejection_time_ms_; }
    uint64_t consecutive5xx() const override { return consecutive_5xx_; }
    uint64_t maxEjectionPercent() const override { return max_ejection_percent_; }
    uint64_t successRateMinimumHosts() const override { return success_rate_minimum_hosts_; }
    uint64_t successRateRequestVolume() const override { return success_rate_request_volume_; }
    uint64_t successRateStdevFactor() const override { return success_rate_stdev_factor_; }
    uint64_t enforcingConsecutive5xx() const override { return enforcing_consecutive_5xx_; }
    uint64_t enforcingSuccessRate() const override { return enforcing_success_rate_; }

   private:
    const uint64_t interval_ms_;
    const uint64_t base_ejection_time_ms_;
    const uint64_t consecutive_5xx_;
    const uint64_t max_ejection_percent_;
    const uint64_t success_rate_minimum_hosts_;
    const uint64_t success_rate_request_volume_;
    const uint64_t success_rate_stdev_factor_;
    const uint64_t enforcing_consecutive_5xx_;
    const uint64_t enforcing_success_rate_;
  };

  class CircuitBreakerConfigImpl : public CircuitBreakerConfig {
  public:
    CircuitBreakerConfigImpl(const envoy::api::v2::CircuitBreakers& config);
    virtual ~CircuitBreakerConfigImpl() {}

    const std::vector<Thresholds::ConstSharedPtr>& thresholds() const override {
      return thresholds_;
    }

  public:

    class ThresholdsImpl : public Thresholds {
    public:
      ThresholdsImpl(const envoy::api::v2::CircuitBreakers_Thresholds& config);
      virtual ~ThresholdsImpl() {}

      envoy::api::v2::RoutingPriority priority() const override {
        return priority_;
      }

      uint32_t maxConnections() const override {
        return max_connections_;
      }
      uint32_t maxPendingRequests() const override {
        return max_pending_requests_;
      }

      uint32_t maxRequests() const override {
        return max_requests_;
      }

      uint32_t maxRetries() const override {
        return max_retries_;
      }

    private:

      envoy::api::v2::RoutingPriority priority_;
      uint32_t max_connections_;
      uint32_t max_pending_requests_;
      uint32_t max_requests_;
      uint32_t max_retries_;
    };

  private:

    std::vector<Thresholds::ConstSharedPtr> thresholds_;
  };

private:

  const std::string name_;
  const uint64_t max_requests_per_connection_;
  const std::chrono::milliseconds connect_timeout_;
  const uint32_t per_connection_buffer_limit_bytes_;
  const uint64_t features_;
  const Http::Http2Settings http2_settings_;
  const Network::Address::InstanceConstSharedPtr source_address_;

  const Ssl::ClientContextConfig::ConstSharedPtr tls_context_config_;
  const LoadBalancerType lb_type_;

  // TODO(nmittler): This currently references an underlying proto message.
  const LoadBalancerSubsetInfoImpl lb_subset_info_;

  const OutlierDetectionConfig::ConstSharedPtr outlier_detection_config_;

  const CircuitBreakerConfigImpl circuit_breaker_config_;
};


} // namespace Upstream
} // namespace Envoy