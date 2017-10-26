#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/http/codec.h"
#include "envoy/network/address.h"
#include "envoy/ssl/context_config.h"
#include "envoy/upstream/load_balancer_type.h"

namespace Envoy {
namespace Upstream {

/**
 * Configuration for an upstream cluster.
 */
class ClusterConfig {
public:

  /**
   * The configuration for outlier detection for the upstream cluster.
   */
  class OutlierDetectionConfig {
  public:
    virtual ~OutlierDetectionConfig() {}

    virtual uint64_t intervalMs() const PURE;
    virtual uint64_t baseEjectionTimeMs() const PURE;
    virtual uint64_t consecutive5xx() const PURE;
    virtual uint64_t maxEjectionPercent() const PURE;
    virtual uint64_t successRateMinimumHosts() const PURE;
    virtual uint64_t successRateRequestVolume() const PURE;
    virtual uint64_t successRateStdevFactor() const PURE;
    virtual uint64_t enforcingConsecutive5xx() const PURE;
    virtual uint64_t enforcingSuccessRate() const PURE;

    typedef std::shared_ptr<const OutlierDetectionConfig> ConstSharedPtr;
  };

  class CircuitBreakerConfig {
  public:

    class Thresholds {
    public:
      virtual ~Thresholds() {}

      virtual envoy::api::v2::RoutingPriority priority() const PURE;
      virtual uint32_t maxConnections() const PURE;
      virtual uint32_t maxPendingRequests() const PURE;
      virtual uint32_t maxRequests() const PURE;
      virtual uint32_t maxRetries() const PURE;

      typedef std::shared_ptr<const Thresholds> ConstSharedPtr;
    };

  public:

    virtual ~CircuitBreakerConfig() {}

    virtual const std::vector<Thresholds::ConstSharedPtr>& thresholds() const PURE;
  };

public:
  virtual ~ClusterConfig() {}

  virtual const std::string& name() const PURE;

  virtual uint64_t maxRequestsPerConnection() const PURE;

  virtual const std::chrono::milliseconds& connectTimeout() const PURE;

  virtual uint32_t perConnectionBufferLimitBytes() const PURE;

  virtual uint64_t features() const PURE;

  virtual const Http::Http2Settings& http2Settings() const PURE;

  virtual const Network::Address::InstanceConstSharedPtr& sourceAddress() const PURE;

  virtual const Ssl::ClientContextConfig::ConstSharedPtr& tlsContextConfig() const PURE;

  virtual LoadBalancerType lbType() const PURE;

  virtual const LoadBalancerSubsetInfo& lbSubsetInfo() const PURE;

  virtual const OutlierDetectionConfig::ConstSharedPtr& outlierDetectionConfig() const PURE;

  virtual const CircuitBreakerConfig& circuitBreakerConfig() const PURE;

  typedef std::shared_ptr<const ClusterConfig> ConstSharedPtr;
};

} // namespace Upstream
} // namespace Envoy