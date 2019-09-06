#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/api/v2/cluster/outlier_detection.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/stats/fake_symbol_table_impl.h"

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

  MOCK_METHOD0(numEjections, uint32_t());
  MOCK_METHOD1(putHttpResponseCode, void(uint64_t code));
  MOCK_METHOD2(putResult, void(Result result, absl::optional<uint64_t> code));
  MOCK_METHOD1(putResponseTime, void(std::chrono::milliseconds time));
  MOCK_METHOD0(lastEjectionTime, const absl::optional<MonotonicTime>&());
  MOCK_METHOD0(lastUnejectionTime, const absl::optional<MonotonicTime>&());
  MOCK_CONST_METHOD1(successRate, double(DetectorHostMonitor::SuccessRateMonitorType type));
  MOCK_METHOD2(successRate,
               void(DetectorHostMonitor::SuccessRateMonitorType type, double new_success_rate));
};

class MockEventLogger : public EventLogger {
public:
  MockEventLogger();
  ~MockEventLogger() override;

  MOCK_METHOD4(logEject,
               void(const HostDescriptionConstSharedPtr& host, Detector& detector,
                    envoy::data::cluster::v2alpha::OutlierEjectionType type, bool enforced));
  MOCK_METHOD1(logUneject, void(const HostDescriptionConstSharedPtr& host));
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

  MOCK_METHOD1(addChangedStateCb, void(ChangeStateCb cb));
  MOCK_CONST_METHOD1(successRateAverage, double(DetectorHostMonitor::SuccessRateMonitorType));
  MOCK_CONST_METHOD1(successRateEjectionThreshold,
                     double(DetectorHostMonitor::SuccessRateMonitorType));

  std::list<ChangeStateCb> callbacks_;
};

} // namespace Outlier

class MockHealthCheckHostMonitor : public HealthCheckHostMonitor {
public:
  MockHealthCheckHostMonitor();
  ~MockHealthCheckHostMonitor() override;

  MOCK_METHOD0(setUnhealthy, void());
};

class MockHostDescription : public HostDescription {
public:
  MockHostDescription();
  ~MockHostDescription() override;

  MOCK_CONST_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(healthCheckAddress, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(canary, bool());
  MOCK_METHOD1(canary, void(bool new_canary));
  MOCK_CONST_METHOD0(metadata, const std::shared_ptr<envoy::api::v2::core::Metadata>());
  MOCK_METHOD1(metadata, void(const envoy::api::v2::core::Metadata&));
  MOCK_CONST_METHOD0(cluster, const ClusterInfo&());
  MOCK_CONST_METHOD0(outlierDetector, Outlier::DetectorHostMonitor&());
  MOCK_CONST_METHOD0(healthChecker, HealthCheckHostMonitor&());
  MOCK_CONST_METHOD0(hostname, const std::string&());
  MOCK_CONST_METHOD0(stats, HostStats&());
  MOCK_CONST_METHOD0(locality, const envoy::api::v2::core::Locality&());
  MOCK_CONST_METHOD0(priority, uint32_t());
  MOCK_METHOD1(priority, void(uint32_t));
  Stats::StatName localityZoneStatName() const override {
    Stats::SymbolTable& symbol_table = *symbol_table_;
    locality_zone_stat_name_ =
        std::make_unique<Stats::StatNameManagedStorage>(locality().zone(), symbol_table);
    return locality_zone_stat_name_->statName();
  }

  std::string hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  testing::NiceMock<Outlier::MockDetectorHostMonitor> outlier_detector_;
  testing::NiceMock<MockHealthCheckHostMonitor> health_checker_;
  testing::NiceMock<MockClusterInfo> cluster_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  HostStats stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_), POOL_GAUGE(stats_store_))};
  mutable Stats::TestSymbolTable symbol_table_;
  mutable std::unique_ptr<Stats::StatNameManagedStorage> locality_zone_stat_name_;
};

class MockHost : public Host {
public:
  struct MockCreateConnectionData {
    Network::ClientConnection* connection_{};
    HostDescriptionConstSharedPtr host_description_{};
  };

  MockHost();
  ~MockHost() override;

  CreateConnectionData createConnection(Event::Dispatcher& dispatcher,
                                        const Network::ConnectionSocket::OptionsSharedPtr& options,
                                        Network::TransportSocketOptionsSharedPtr) const override {
    MockCreateConnectionData data = createConnection_(dispatcher, options);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  CreateConnectionData createHealthCheckConnection(Event::Dispatcher& dispatcher) const override {
    MockCreateConnectionData data = createConnection_(dispatcher, nullptr);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  void setHealthChecker(HealthCheckHostMonitorPtr&& health_checker) override {
    setHealthChecker_(health_checker);
  }

  void setOutlierDetector(Outlier::DetectorHostMonitorPtr&& outlier_detector) override {
    setOutlierDetector_(outlier_detector);
  }

  Stats::StatName localityZoneStatName() const override {
    locality_zone_stat_name_ =
        std::make_unique<Stats::StatNameManagedStorage>(locality().zone(), *symbol_table_);
    return locality_zone_stat_name_->statName();
  }

  MOCK_CONST_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(healthCheckAddress, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(canary, bool());
  MOCK_METHOD1(canary, void(bool new_canary));
  MOCK_CONST_METHOD0(metadata, const std::shared_ptr<envoy::api::v2::core::Metadata>());
  MOCK_METHOD1(metadata, void(const envoy::api::v2::core::Metadata&));
  MOCK_CONST_METHOD0(cluster, const ClusterInfo&());
  MOCK_CONST_METHOD0(counters, std::vector<Stats::CounterSharedPtr>());
  MOCK_CONST_METHOD2(
      createConnection_,
      MockCreateConnectionData(Event::Dispatcher& dispatcher,
                               const Network::ConnectionSocket::OptionsSharedPtr& options));
  MOCK_CONST_METHOD0(gauges, std::vector<Stats::GaugeSharedPtr>());
  MOCK_CONST_METHOD0(healthChecker, HealthCheckHostMonitor&());
  MOCK_METHOD1(healthFlagClear, void(HealthFlag flag));
  MOCK_CONST_METHOD1(healthFlagGet, bool(HealthFlag flag));
  MOCK_CONST_METHOD0(getActiveHealthFailureType, ActiveHealthFailureType());
  MOCK_METHOD1(healthFlagSet, void(HealthFlag flag));
  MOCK_METHOD1(setActiveHealthFailureType, void(ActiveHealthFailureType type));
  MOCK_CONST_METHOD0(health, Host::Health());
  MOCK_CONST_METHOD0(hostname, const std::string&());
  MOCK_CONST_METHOD0(outlierDetector, Outlier::DetectorHostMonitor&());
  MOCK_METHOD1(setHealthChecker_, void(HealthCheckHostMonitorPtr& health_checker));
  MOCK_METHOD1(setOutlierDetector_, void(Outlier::DetectorHostMonitorPtr& outlier_detector));
  MOCK_CONST_METHOD0(stats, HostStats&());
  MOCK_CONST_METHOD0(weight, uint32_t());
  MOCK_METHOD1(weight, void(uint32_t new_weight));
  MOCK_CONST_METHOD0(used, bool());
  MOCK_METHOD1(used, void(bool new_used));
  MOCK_CONST_METHOD0(locality, const envoy::api::v2::core::Locality&());
  MOCK_CONST_METHOD0(priority, uint32_t());
  MOCK_METHOD1(priority, void(uint32_t));
  MOCK_CONST_METHOD0(warmed, bool());

  testing::NiceMock<MockClusterInfo> cluster_;
  testing::NiceMock<Outlier::MockDetectorHostMonitor> outlier_detector_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  HostStats stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_), POOL_GAUGE(stats_store_))};
  mutable Stats::TestSymbolTable symbol_table_;
  mutable std::unique_ptr<Stats::StatNameManagedStorage> locality_zone_stat_name_;
};

} // namespace Upstream
} // namespace Envoy
