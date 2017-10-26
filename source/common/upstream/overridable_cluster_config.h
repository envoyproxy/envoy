#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "common/common/utility.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/cluster_config.h"

namespace Envoy {
namespace Upstream {

#define OVERRIDE_CLUSTER_PROP_WITH_HEADER(propertyName, headerMethodName) \
    {                                                                     \
      const Http::HeaderEntry* entry = headers.headerMethodName();        \
      uint64_t value;                                                     \
      if (entry && StringUtil::atoul(entry->value().c_str(), value)) {    \
        propertyName(value);                                              \
      }                                                                   \
    }

// A macro for defining the override methods.
#define OVERRIDABLE_CLUSTER_PROP_UINT64(propertyName, fieldIndex) \
  private:                                                        \
    uint64_t propertyName##_;                                     \
  public:                                                         \
    uint64_t propertyName() const override {                      \
      if (isFieldSet(fieldIndex)) {                               \
        return propertyName##_;                                   \
      }                                                           \
      return delegate().propertyName();                           \
    }                                                             \
    void propertyName(uint64_t value) {                           \
      setField(fieldIndex);                                       \
      propertyName##_ = value;                                    \
    }                                                             \
    void clear##propertyName() {                                  \
      clearField(fieldIndex);                                     \
    }

class OverridableClusterConfig : public virtual ClusterConfig {
public:

  class OverridingOutlierDetectionConfig : public OutlierDetectionConfig {
    // Give the parent class full access.
    friend class OverridableClusterConfig;

  public:
    OverridingOutlierDetectionConfig(OverridableClusterConfig* parent)
        : parent_(parent),
          bit_field_(0) {}

    virtual ~OverridingOutlierDetectionConfig() {}

    OVERRIDABLE_CLUSTER_PROP_UINT64(intervalMs, 0)
    OVERRIDABLE_CLUSTER_PROP_UINT64(baseEjectionTimeMs, 1)
    OVERRIDABLE_CLUSTER_PROP_UINT64(consecutive5xx, 2)
    OVERRIDABLE_CLUSTER_PROP_UINT64(maxEjectionPercent, 3)
    OVERRIDABLE_CLUSTER_PROP_UINT64(successRateMinimumHosts, 4)
    OVERRIDABLE_CLUSTER_PROP_UINT64(successRateRequestVolume, 5)
    OVERRIDABLE_CLUSTER_PROP_UINT64(successRateStdevFactor, 6)
    OVERRIDABLE_CLUSTER_PROP_UINT64(enforcingConsecutive5xx, 7)
    OVERRIDABLE_CLUSTER_PROP_UINT64(enforcingSuccessRate, 8)

    typedef std::shared_ptr<OverridingOutlierDetectionConfig> SharedPtr;

  private:

    void overrideWithHeaders(const Http::HeaderMap& headers) {
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(intervalMs, EnvoyOutlierIntervalMs)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(baseEjectionTimeMs, EnvoyOutlierBaseEjectionTimeMs)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(consecutive5xx, EnvoyOutlierConsecutive5xx)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(maxEjectionPercent, EnvoyOutlierMaxEjectionPercent)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(successRateMinimumHosts, EnvoyOutlierSuccessRateMinimumHosts)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(successRateRequestVolume, EnvoyOutlierSuccessRateRequestVolume)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(successRateStdevFactor, EnvoyOutlierSuccessRateStdevFactor)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(enforcingConsecutive5xx, EnvoyOutlierEnforcingConsecutive5xx)
      OVERRIDE_CLUSTER_PROP_WITH_HEADER(enforcingSuccessRate, EnvoyOutlierEnforcingSuccessRate)
    }

    void clear() {
      bit_field_ = 0;
    }

    const OutlierDetectionConfig& delegate() const {
      return *parent_->delegate_->outlierDetectionConfig();
    }

    bool isFieldSet(int fieldIndex) const {
      return (bit_field_ & (1 << fieldIndex)) != 0;
    }

    void setField(int fieldIndex) {
      bit_field_ |= (1 << fieldIndex);
    }

    void clearField(int fieldIndex) {
      bit_field_ &= ~(1 << fieldIndex);
    }

  private:

    const OverridableClusterConfig* parent_;
    uint32_t bit_field_;
  };

public:

  OverridableClusterConfig(const ClusterConfig::ConstSharedPtr& delegate)
  : delegate_(delegate) {

    if (delegate_->outlierDetectionConfig() != nullptr) {
      overridable_outlier_detection_config_ = std::make_shared<OverridingOutlierDetectionConfig>(this);
      outlier_detection_config_ = std::static_pointer_cast<OutlierDetectionConfig>(overridable_outlier_detection_config_);
    }
  }

  virtual ~OverridableClusterConfig() {
  }

  const std::string& name() const override {
    return delegate_->name();
  }

  uint64_t maxRequestsPerConnection() const override {
    return delegate_->maxRequestsPerConnection();
  }

  const std::chrono::milliseconds& connectTimeout() const override {
    return delegate_->connectTimeout();
  }

  uint32_t perConnectionBufferLimitBytes() const override {
    return delegate_->perConnectionBufferLimitBytes();
  }

  uint64_t features() const override {
    return delegate_->features();
  }

  const Http::Http2Settings& http2Settings() const override {
    return delegate_->http2Settings();
  }

  const Network::Address::InstanceConstSharedPtr& sourceAddress() const override {
    if (source_address_ != nullptr) {
      return source_address_;
    }
    return delegate_->sourceAddress();
  }

  void sourceAddress(const Network::Address::InstanceConstSharedPtr sourceAddress) {
    source_address_ = sourceAddress;
  }

  const Ssl::ClientContextConfig::ConstSharedPtr& tlsContextConfig() const override {
    return delegate_->tlsContextConfig();
  }

  LoadBalancerType lbType() const override {
    return delegate_->lbType();
  }

  const LoadBalancerSubsetInfo& lbSubsetInfo() const override {
    return delegate_->lbSubsetInfo();
  }

  const OutlierDetectionConfig::ConstSharedPtr& outlierDetectionConfig() const override {
    return outlier_detection_config_;
  }

  OverridingOutlierDetectionConfig::SharedPtr& overridableOutlierDetectionConfig() {
    return overridable_outlier_detection_config_;
  }

  const CircuitBreakerConfig& circuitBreakerConfig() const override {
    return delegate_->circuitBreakerConfig();
  }

  void clearOverrides() {
    if (hasOutlierDetectionConfig()) {
      overridable_outlier_detection_config_->clear();
    }
  }

  void overrideWithHeaders(const Http::HeaderMap& headers) {
    if (hasOutlierDetectionConfig()) {
      overridable_outlier_detection_config_->overrideWithHeaders(headers);
    }
  }

private:

  bool hasOutlierDetectionConfig() const {
    return overridable_outlier_detection_config_ != nullptr;
  }

private:

  const ClusterConfig::ConstSharedPtr delegate_;
  Network::Address::InstanceConstSharedPtr source_address_;

  OutlierDetectionConfig::ConstSharedPtr outlier_detection_config_;
  OverridingOutlierDetectionConfig::SharedPtr overridable_outlier_detection_config_;
};

}  // namespace Upstream
}  // namespace Envoy

