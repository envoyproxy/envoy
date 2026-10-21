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

#include "absl/container/inlined_vector.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {
namespace Outlier {

class MockDetectorHostMonitor : public DetectorHostMonitor {
public:
  MockDetectorHostMonitor();
  ~MockDetectorHostMonitor() override;

  MOCK_METHOD(uint32_t, numEjections, (), (override));
  MOCK_METHOD(void, putResult, (Result result, std::optional<uint64_t> code), (override));
  MOCK_METHOD(void, putResponseTime, (std::chrono::milliseconds time), (override));
  MOCK_METHOD(const std::optional<MonotonicTime>&, lastEjectionTime, (), (override));
  MOCK_METHOD(const std::optional<MonotonicTime>&, lastUnejectionTime, (), (override));
  MOCK_METHOD(double, successRate, (DetectorHostMonitor::SuccessRateMonitorType type),
              (const, override));
  MOCK_METHOD(void, successRate,
              (DetectorHostMonitor::SuccessRateMonitorType type, double new_success_rate));
};

class MockEventLogger : public EventLogger {
public:
  MockEventLogger();
  ~MockEventLogger() override;

  MOCK_METHOD(void, logEject,
              (const HostDescriptionConstSharedPtr& host, Detector& detector,
               envoy::data::cluster::v3::OutlierEjectionType type, bool enforced),
              (override));
  MOCK_METHOD(void, logUneject, (const HostDescriptionConstSharedPtr& host), (override));
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

  MOCK_METHOD(void, addChangedStateCb, (ChangeStateCb cb), (override));
  MOCK_METHOD(double, successRateAverage, (DetectorHostMonitor::SuccessRateMonitorType),
              (const, override));
  MOCK_METHOD(double, successRateEjectionThreshold, (DetectorHostMonitor::SuccessRateMonitorType),
              (const, override));

  std::list<ChangeStateCb> callbacks_;
};

} // namespace Outlier

class MockHealthCheckHostMonitor : public HealthCheckHostMonitor {
public:
  MockHealthCheckHostMonitor();
  ~MockHealthCheckHostMonitor() override;

  MOCK_METHOD(void, setUnhealthy, (UnhealthyType), (override));
};

class MockHostLbPolicyData : public HostLbPolicyData {
public:
  explicit MockHostLbPolicyData(bool receives_orca_load_report = true);
  ~MockHostLbPolicyData() override;

  bool receivesOrcaLoadReport() const override { return receives_orca_load_report_; }
  MOCK_METHOD(absl::Status, onOrcaLoadReport,
              (const OrcaLoadReport& report, OptRef<const StreamInfo::StreamInfo> stream_info),
              (override));

  const bool receives_orca_load_report_;
};

class MockHostDescription : public HostDescription {
public:
  MockHostDescription();
  ~MockHostDescription() override;

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, (), (const, override));
  MOCK_METHOD(SharedConstAddressVector, addressListOrNull, (), (const, override));
  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, healthCheckAddress, (), (const, override));
  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, orcaReportingAddress, (),
              (const, override));
  MOCK_METHOD(bool, canary, (), (const, override));
  MOCK_METHOD(void, canary, (bool new_canary), (override));
  MOCK_METHOD(MetadataConstSharedPtr, metadata, (), (const, override));
  MOCK_METHOD(std::size_t, metadataHash, (), (const, override));
  MOCK_METHOD(void, metadata, (MetadataConstSharedPtr), (override));
  MOCK_METHOD(const MetadataConstSharedPtr, localityMetadata, (), (const, override));
  MOCK_METHOD(const ClusterInfo&, cluster, (), (const, override));
  MOCK_METHOD(bool, canCreateConnection, (Upstream::ResourcePriority), (const, override));
  MOCK_METHOD(Outlier::DetectorHostMonitor&, outlierDetector, (), (const, override));
  MOCK_METHOD(void, setOutlierDetector, (Outlier::DetectorHostMonitorPtr && outlier_detector),
              (override));
  MOCK_METHOD(HealthCheckHostMonitor&, healthChecker, (), (const, override));
  MOCK_METHOD(void, setHealthChecker, (HealthCheckHostMonitorPtr && health_checker), (override));
  MOCK_METHOD(const std::string&, hostnameForHealthChecks, (), (const, override));
  MOCK_METHOD(const std::string&, hostname, (), (const, override));
  MOCK_METHOD(absl::string_view, observabilityName, (), (const, override));
  MOCK_METHOD(Network::UpstreamTransportSocketFactory&, transportSocketFactory, (),
              (const, override));
  MOCK_METHOD(HostStats&, stats, (), (const, override));
  MOCK_METHOD(LoadMetricStats&, loadMetricStats, (), (const, override));
  MOCK_METHOD(const envoy::config::core::v3::Locality&, locality, (), (const, override));
  MOCK_METHOD(uint32_t, priority, (), (const, override));
  MOCK_METHOD(void, priority, (uint32_t), (override));
  MOCK_METHOD(std::optional<MonotonicTime>, lastHcPassTime, (), (const, override));
  MOCK_METHOD(void, setLastHcPassTime, (MonotonicTime last_hc_pass_time), (override));
  Stats::StatName localityZoneStatName() const override {
    locality_zone_stat_name_ =
        std::make_unique<Stats::StatNameManagedStorage>(locality().zone(), *symbol_table_);
    return locality_zone_stat_name_->statName();
  }
  MOCK_METHOD(Network::UpstreamTransportSocketFactory&, resolveTransportSocketFactory,
              (const Network::Address::InstanceConstSharedPtr& dest_address,
               const envoy::config::core::v3::Metadata* metadata,
               Network::TransportSocketOptionsConstSharedPtr transport_socket_options),
              (const, override));
  MOCK_METHOD(void, addLbPolicyData, (HostLbPolicyDataPtr lb_policy_data), (override));
  MOCK_METHOD(size_t, lbPolicyDataCount, (), (const, override));
  MOCK_METHOD(OptRef<HostLbPolicyData>, lbPolicyDataAt, (size_t index), (const, override));

  std::string hostname_;
  std::string observability_name_;
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

private:
  absl::InlinedVector<HostLbPolicyDataPtr, 2> lb_policy_datas_;
};

class MockHostLight : public Host {
public:
  MockHostLight();
  ~MockHostLight() override;

  struct MockCreateConnectionData {
    Network::ClientConnection* connection_{};
    HostDescriptionConstSharedPtr host_description_;
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

  CreateConnectionData
  createOrcaReportingConnection(Event::Dispatcher& dispatcher,
                                Network::TransportSocketOptionsConstSharedPtr,
                                Network::UpstreamTransportSocketFactory&,
                                Network::Address::InstanceConstSharedPtr) const override {
    MockCreateConnectionData data = createConnection_(dispatcher, nullptr);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  bool disableActiveHealthCheck() const override { return disable_active_health_check_; }
  void setDisableActiveHealthCheck(bool disable_active_health_check) override {
    disable_active_health_check_ = disable_active_health_check;
  }

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, (), (const, override));
  MOCK_METHOD(SharedConstAddressVector, addressListOrNull, (), (const, override));
  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, healthCheckAddress, (), (const, override));
  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, orcaReportingAddress, (),
              (const, override));
  MOCK_METHOD(bool, canary, (), (const, override));
  MOCK_METHOD(void, canary, (bool new_canary), (override));
  MOCK_METHOD(MetadataConstSharedPtr, metadata, (), (const, override));
  MOCK_METHOD(std::size_t, metadataHash, (), (const, override));
  MOCK_METHOD(const MetadataConstSharedPtr, localityMetadata, (), (const, override));
  MOCK_METHOD(void, metadata, (MetadataConstSharedPtr), (override));
  MOCK_METHOD(const ClusterInfo&, cluster, (), (const, override));
  MOCK_METHOD(bool, canCreateConnection, (Upstream::ResourcePriority), (const, override));
  MOCK_METHOD((std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>),
              counters, (), (const, override));
  MOCK_METHOD(MockCreateConnectionData, createConnection_,
              (Event::Dispatcher & dispatcher,
               const Network::ConnectionSocket::OptionsSharedPtr& options),
              (const));
  MOCK_METHOD((std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>), gauges,
              (), (const, override));
  MOCK_METHOD(HealthCheckHostMonitor&, healthChecker, (), (const, override));
  MOCK_METHOD(void, healthFlagClear, (HealthFlag flag), (override));
  MOCK_METHOD(bool, healthFlagGet, (HealthFlag flag), (const, override));
  MOCK_METHOD(void, healthFlagSet, (HealthFlag flag), (override));
  MOCK_METHOD(uint32_t, healthFlagsGetAll, (), (const, override));
  MOCK_METHOD(void, healthFlagsSetAll, (uint32_t), (override));
  MOCK_METHOD(Host::Health, coarseHealth, (), (const, override));
  MOCK_METHOD(Host::HealthStatus, healthStatus, (), (const, override));
  MOCK_METHOD(Host::HealthStatus, edsHealthStatus, (), (const, override));
  MOCK_METHOD(void, setEdsHealthStatus, (Host::HealthStatus), (override));

  MOCK_METHOD(const std::string&, hostnameForHealthChecks, (), (const, override));
  MOCK_METHOD(const std::string&, hostname, (), (const, override));
  MOCK_METHOD(absl::string_view, observabilityName, (), (const, override));
  MOCK_METHOD(Network::UpstreamTransportSocketFactory&, transportSocketFactory, (),
              (const, override));
  MOCK_METHOD(Outlier::DetectorHostMonitor&, outlierDetector, (), (const, override));
  MOCK_METHOD(void, setHealthChecker, (HealthCheckHostMonitorPtr && health_checker), (override));
  MOCK_METHOD(void, setOutlierDetector, (Outlier::DetectorHostMonitorPtr && outlier_detector),
              (override));
  MOCK_METHOD(void, setLastHcPassTime, (MonotonicTime last_hc_pass_time), (override));
  MOCK_METHOD(HostStats&, stats, (), (const, override));
  MOCK_METHOD(LoadMetricStats&, loadMetricStats, (), (const, override));
  MOCK_METHOD(uint32_t, weight, (), (const, override));
  MOCK_METHOD(void, weight, (uint32_t new_weight), (override));
  MOCK_METHOD(bool, used, (), (const, override));
  MOCK_METHOD(HostHandlePtr, acquireHandle, (), (const, override));

  MOCK_METHOD(const envoy::config::core::v3::Locality&, locality, (), (const, override));
  MOCK_METHOD(Stats::StatName, localityZoneStatName, (), (const, override));
  MOCK_METHOD(uint32_t, priority, (), (const, override));
  MOCK_METHOD(void, priority, (uint32_t), (override));
  MOCK_METHOD(bool, warmed, (), (const));
  MOCK_METHOD(std::optional<MonotonicTime>, lastHcPassTime, (), (const, override));
  MOCK_METHOD(void, addLbPolicyData, (HostLbPolicyDataPtr lb_policy_data), (override));
  MOCK_METHOD(size_t, lbPolicyDataCount, (), (const, override));
  MOCK_METHOD(OptRef<HostLbPolicyData>, lbPolicyDataAt, (size_t index), (const, override));
  MOCK_METHOD(void, setLastHealthCheckHttpStatus, (uint64_t), (override));
  MOCK_METHOD(std::optional<uint64_t>, lastHealthCheckHttpStatus, (), (const, override));

  bool disable_active_health_check_ = false;
  std::string observability_name_;
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
               const envoy::config::core::v3::Metadata* metadata,
               Network::TransportSocketOptionsConstSharedPtr transport_socket_options),
              (const, override));

  testing::NiceMock<MockClusterInfo> cluster_;
  Network::UpstreamTransportSocketFactoryPtr socket_factory_;
  testing::NiceMock<Outlier::MockDetectorHostMonitor> outlier_detector_;
  HostStats stats_;
  LoadMetricStatsImpl load_metric_stats_;
  mutable Stats::TestUtil::TestSymbolTable symbol_table_;
  mutable std::unique_ptr<Stats::StatNameManagedStorage> locality_zone_stat_name_;

private:
  absl::InlinedVector<HostLbPolicyDataPtr, 2> lb_policy_datas_;
};

} // namespace Upstream
} // namespace Envoy
