#include "host_set.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;
MockHostSet::MockHostSet(uint32_t priority, uint32_t overprovisioning_factor)
    : priority_(priority), overprovisioning_factor_(overprovisioning_factor) {
  ON_CALL(*this, priority()).WillByDefault(Return(priority_));
  ON_CALL(*this, hosts()).WillByDefault(ReturnRef(hosts_));
  ON_CALL(*this, hostsPtr()).WillByDefault(Invoke([this]() {
    return std::make_shared<HostVector>(hosts_);
  }));
  ON_CALL(*this, healthyHosts()).WillByDefault(ReturnRef(healthy_hosts_));
  ON_CALL(*this, healthyHostsPtr()).WillByDefault(Invoke([this]() {
    return std::make_shared<HealthyHostVector>(healthy_hosts_);
  }));
  ON_CALL(*this, degradedHosts()).WillByDefault(ReturnRef(degraded_hosts_));
  ON_CALL(*this, degradedHostsPtr()).WillByDefault(Invoke([this]() {
    return std::make_shared<DegradedHostVector>(degraded_hosts_);
  }));
  ON_CALL(*this, excludedHosts()).WillByDefault(ReturnRef(excluded_hosts_));
  ON_CALL(*this, excludedHostsPtr()).WillByDefault(Invoke([this]() {
    return std::make_shared<ExcludedHostVector>(excluded_hosts_);
  }));
  ON_CALL(*this, hostsPerLocality()).WillByDefault(Invoke([this]() -> const HostsPerLocality& {
    return *hosts_per_locality_;
  }));
  ON_CALL(*this, hostsPerLocalityPtr()).WillByDefault(Return(hosts_per_locality_));
  ON_CALL(*this, healthyHostsPerLocality())
      .WillByDefault(
          Invoke([this]() -> const HostsPerLocality& { return *healthy_hosts_per_locality_; }));
  ON_CALL(*this, healthyHostsPerLocalityPtr()).WillByDefault(Return(healthy_hosts_per_locality_));
  ON_CALL(*this, degradedHostsPerLocality())
      .WillByDefault(
          Invoke([this]() -> const HostsPerLocality& { return *degraded_hosts_per_locality_; }));
  ON_CALL(*this, degradedHostsPerLocalityPtr()).WillByDefault(Return(degraded_hosts_per_locality_));
  ON_CALL(*this, excludedHostsPerLocality())
      .WillByDefault(
          Invoke([this]() -> const HostsPerLocality& { return *excluded_hosts_per_locality_; }));
  ON_CALL(*this, excludedHostsPerLocalityPtr()).WillByDefault(Return(excluded_hosts_per_locality_));
  ON_CALL(*this, localityWeights()).WillByDefault(Invoke([this]() -> LocalityWeightsConstSharedPtr {
    return locality_weights_;
  }));
}

MockHostSet::~MockHostSet() = default;

} // namespace Upstream

} // namespace Envoy
