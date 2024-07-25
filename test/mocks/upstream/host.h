#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/cluster/v3/outlier_detection_event.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/stats/symbol_table.h"

#include "test/mocks/network/transport_socket.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/global.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {
namespace Outlier {

class MockDetectorHostMonitor : public DetectorHostMonitor {
public:
  MockDetectorHostMonitor();
  ~MockDetectorHostMonitor() override;

  MOCK_METHOD(uint32_t, numEjections, ());
  MOCK_METHOD(void, putHttpResponseCode, (uint64_t code));
  MOCK_METHOD(void, putResult, (Result result, absl::optional<uint64_t> code));
  MOCK_METHOD(void, putResponseTime, (std::chrono::milliseconds time));
  MOCK_METHOD(const absl::optional<MonotonicTime>&, lastEjectionTime, ());
  MOCK_METHOD(const absl::optional<MonotonicTime>&, lastUnejectionTime, ());
  MOCK_METHOD(double, successRate, (DetectorHostMonitor::SuccessRateMonitorType type), (const));
  MOCK_METHOD(void, successRate,
              (DetectorHostMonitor::SuccessRateMonitorType type, double new_success_rate));
};

class MockEventLogger : public EventLogger {
public:
  MockEventLogger();
  ~MockEventLogger() override;

  MOCK_METHOD(void, logEject,
              (const HostDescriptionConstSharedPtr& host, Detector& detector,
               envoy::data::cluster::v3::OutlierEjectionType type, bool enforced));
  MOCK_METHOD(void, logUneject, (const HostDescriptionConstSharedPtr& host));
};

class MockDetector : public Detector {
public:
  MockDetector();
  ~MockDetector() override;

  void runCallbacks(HostSharedPtr host) {
    for (const ChangeStateCb& cb : callbacks_) {
      cb(host);
    }
  }

  MOCK_METHOD(void, addChangedStateCb, (ChangeStateCb cb));
  MOCK_METHOD(double, successRateAverage, (DetectorHostMonitor::SuccessRateMonitorType), (const));
  MOCK_METHOD(double, successRateEjectionThreshold, (DetectorHostMonitor::SuccessRateMonitorType),
              (const));

  std::list<ChangeStateCb> callbacks_;
};

} // namespace Outlier

class MockHealthCheckHostMonitor : public HealthCheckHostMonitor {
public:
  MockHealthCheckHostMonitor();
  ~MockHealthCheckHostMonitor() override;

  MOCK_METHOD(void, setUnhealthy, (UnhealthyType));
};

class MockHostDescription : public HostDescription {
public:
  MockHostDescription();
  ~MockHostDescription() override;

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, (), (const));
  MOCK_METHOD(SharedConstAddressVector, addressListOrNull, (), (const));
  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, healthCheckAddress, (), (const));
  MOCK_METHOD(bool, canary, (), (const));
  MOCK_METHOD(void, canary, (bool new_canary));
  MOCK_METHOD(MetadataConstSharedPtr, metadata, (), (const));
  MOCK_METHOD(void, metadata, (MetadataConstSharedPtr));
  MOCK_METHOD(const ClusterInfo&, cluster, (), (const));
  MOCK_METHOD(bool, canCreateConnection, (Upstream::ResourcePriority), (const));
  MOCK_METHOD(Outlier::DetectorHostMonitor&, outlierDetector, (), (const));
  MOCK_METHOD(void, setOutlierDetector, (Outlier::DetectorHostMonitorPtr && outlier_detector));
  MOCK_METHOD(HealthCheckHostMonitor&, healthChecker, (), (const));
  MOCK_METHOD(void, setHealthChecker, (HealthCheckHostMonitorPtr && health_checker));
  MOCK_METHOD(const std::string&, hostnameForHealthChecks, (), (const));
  MOCK_METHOD(const std::string&, hostname, (), (const));
  MOCK_METHOD(Network::UpstreamTransportSocketFactory&, transportSocketFactory, (), (const));
  MOCK_METHOD(HostStats&, stats, (), (const));
  MOCK_METHOD(LoadMetricStats&, loadMetricStats, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Locality&, locality, (), (const));
  MOCK_METHOD(uint32_t, priority, (), (const));
  MOCK_METHOD(void, priority, (uint32_t));
  MOCK_METHOD(absl::optional<MonotonicTime>, lastHcPassTime, (), (const));
  MOCK_METHOD(void, setLastHcPassTime, (MonotonicTime last_hc_pass_time));
  Stats::StatName localityZoneStatName() const override {
    locality_zone_stat_name_ =
        std::make_unique<Stats::StatNameManagedStorage>(locality().zone(), *symbol_table_);
    return locality_zone_stat_name_->statName();
  }
  MOCK_METHOD(Network::UpstreamTransportSocketFactory&, resolveTransportSocketFactory,
              (const Network::Address::InstanceConstSharedPtr& dest_address,
               const envoy::config::core::v3::Metadata* metadata),
              (const));

  std::string hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  testing::NiceMock<Outlier::MockDetectorHostMonitor> outlier_detector_;
  testing::NiceMock<MockHealthCheckHostMonitor> health_checker_;
  Network::UpstreamTransportSocketFactoryPtr socket_factory_;
  testing::NiceMock<MockClusterInfo> cluster_;
  HostStats stats_;
  LoadMetricStatsImpl load_metric_stats_;
  envoy::config::core::v3::Locality locality_;
  mutable Stats::TestUtil::TestSymbolTable symbol_table_;
  mutable std::unique_ptr<Stats::StatNameManagedStorage> locality_zone_stat_name_;
};

class MockHostLight : public Host {
public:
  MockHostLight();
  ~MockHostLight() override;

  struct MockCreateConnectionData {
    Network::ClientConnection* connection_{};
    HostDescriptionConstSharedPtr host_description_{};
  };

  CreateConnectionData
  createConnection(Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   Network::TransportSocketOptionsConstSharedPtr) const override {
    MockCreateConnectionData data = createConnection_(dispatcher, options);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  CreateConnectionData
  createHealthCheckConnection(Event::Dispatcher& dispatcher,
                              Network::TransportSocketOptionsConstSharedPtr,
                              const envoy::config::core::v3::Metadata*) const override {
    MockCreateConnectionData data = createConnection_(dispatcher, nullptr);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  bool disableActiveHealthCheck() const override { return disable_active_health_check_; }
  void setDisableActiveHealthCheck(bool disable_active_health_check) override {
    disable_active_health_check_ = disable_active_health_check;
  }

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, (), (const));
  MOCK_METHOD(SharedConstAddressVector, addressListOrNull, (), (const));
  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, healthCheckAddress, (), (const));
  MOCK_METHOD(bool, canary, (), (const));
  MOCK_METHOD(void, canary, (bool new_canary));
  MOCK_METHOD(MetadataConstSharedPtr, metadata, (), (const));
  MOCK_METHOD(void, metadata, (MetadataConstSharedPtr));
  MOCK_METHOD(const ClusterInfo&, cluster, (), (const));
  MOCK_METHOD(bool, canCreateConnection, (Upstream::ResourcePriority), (const));
  MOCK_METHOD((std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>),
              counters, (), (const));
  MOCK_METHOD(MockCreateConnectionData, createConnection_,
              (Event::Dispatcher & dispatcher,
               const Network::ConnectionSocket::OptionsSharedPtr& options),
              (const));
  MOCK_METHOD((std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>), gauges,
              (), (const));
  MOCK_METHOD(HealthCheckHostMonitor&, healthChecker, (), (const));
  MOCK_METHOD(void, healthFlagClear, (HealthFlag flag));
  MOCK_METHOD(bool, healthFlagGet, (HealthFlag flag), (const));
  MOCK_METHOD(void, healthFlagSet, (HealthFlag flag));
  MOCK_METHOD(uint32_t, healthFlagsGetAll, (), (const));
  MOCK_METHOD(void, healthFlagsSetAll, (uint32_t));
  MOCK_METHOD(Host::Health, coarseHealth, (), (const));
  MOCK_METHOD(Host::HealthStatus, healthStatus, (), (const));
  MOCK_METHOD(Host::HealthStatus, edsHealthStatus, (), (const));
  MOCK_METHOD(void, setEdsHealthStatus, (Host::HealthStatus), ());

  MOCK_METHOD(const std::string&, hostnameForHealthChecks, (), (const));
  MOCK_METHOD(const std::string&, hostname, (), (const));
  MOCK_METHOD(Network::UpstreamTransportSocketFactory&, transportSocketFactory, (), (const));
  MOCK_METHOD(Outlier::DetectorHostMonitor&, outlierDetector, (), (const));
  MOCK_METHOD(void, setHealthChecker, (HealthCheckHostMonitorPtr && health_checker));
  MOCK_METHOD(void, setOutlierDetector, (Outlier::DetectorHostMonitorPtr && outlier_detector));
  MOCK_METHOD(void, setLastHcPassTime, (MonotonicTime last_hc_pass_time));
  MOCK_METHOD(HostStats&, stats, (), (const));
  MOCK_METHOD(LoadMetricStats&, loadMetricStats, (), (const));
  MOCK_METHOD(uint32_t, weight, (), (const));
  MOCK_METHOD(void, weight, (uint32_t new_weight));
  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(HostHandlePtr, acquireHandle, (), (const));

  MOCK_METHOD(const envoy::config::core::v3::Locality&, locality, (), (const));
  MOCK_METHOD(Stats::StatName, localityZoneStatName, (), (const));
  MOCK_METHOD(uint32_t, priority, (), (const));
  MOCK_METHOD(void, priority, (uint32_t));
  MOCK_METHOD(bool, warmed, (), (const));
  MOCK_METHOD(absl::optional<MonotonicTime>, lastHcPassTime, (), (const));
  MOCK_METHOD(void, setLbPolicyData, (HostLbPolicyDataPtr lb_policy_data));
  MOCK_METHOD(const HostLbPolicyDataPtr&, lbPolicyData, (), (const));

  bool disable_active_health_check_ = false;
};

class MockHost : public MockHostLight {
public:
  MockHost();
  ~MockHost() override;

  Stats::StatName localityZoneStatName() const override {
    locality_zone_stat_name_ =
        std::make_unique<Stats::StatNameManagedStorage>(locality().zone(), *symbol_table_);
    return locality_zone_stat_name_->statName();
  }

  MOCK_METHOD(Network::UpstreamTransportSocketFactory&, resolveTransportSocketFactory,
              (const Network::Address::InstanceConstSharedPtr& dest_address,
               const envoy::config::core::v3::Metadata* metadata),
              (const));

  testing::NiceMock<MockClusterInfo> cluster_;
  Network::UpstreamTransportSocketFactoryPtr socket_factory_;
  testing::NiceMock<Outlier::MockDetectorHostMonitor> outlier_detector_;
  HostStats stats_;
  LoadMetricStatsImpl load_metric_stats_;
  mutable Stats::TestUtil::TestSymbolTable symbol_table_;
  mutable std::unique_ptr<Stats::StatNameManagedStorage> locality_zone_stat_name_;
};

} // namespace Upstream
} // namespace Envoy
