#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/upstream/upstream.h"

#include "common/stats/stats_impl.h"

#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {
namespace Outlier {

class MockDetectorHostSink : public DetectorHostSink {
public:
  MockDetectorHostSink();
  ~MockDetectorHostSink();

  MOCK_METHOD0(numEjections, uint32_t());
  MOCK_METHOD1(putHttpResponseCode, void(uint64_t code));
  MOCK_METHOD1(putResponseTime, void(std::chrono::milliseconds time));
  MOCK_METHOD0(lastEjectionTime, const Optional<MonotonicTime>&());
  MOCK_METHOD0(lastUnejectionTime, const Optional<MonotonicTime>&());
  MOCK_CONST_METHOD0(successRate, double());
  MOCK_METHOD1(successRate, void(double new_success_rate));
};

class MockEventLogger : public EventLogger {
public:
  MockEventLogger();
  ~MockEventLogger();

  MOCK_METHOD4(logEject, void(HostDescriptionConstSharedPtr host, Detector& detector,
                              EjectionType type, bool enforced));
  MOCK_METHOD1(logUneject, void(HostDescriptionConstSharedPtr host));
};

class MockDetector : public Detector {
public:
  MockDetector();
  ~MockDetector();

  void runCallbacks(HostSharedPtr host) {
    for (const ChangeStateCb& cb : callbacks_) {
      cb(host);
    }
  }

  MOCK_METHOD1(addChangedStateCb, void(ChangeStateCb cb));
  MOCK_CONST_METHOD0(successRateAverage, double());
  MOCK_CONST_METHOD0(successRateEjectionThreshold, double());

  std::list<ChangeStateCb> callbacks_;
};

} // namespace Outlier

class MockHostDescription : public HostDescription {
public:
  MockHostDescription();
  ~MockHostDescription();

  MOCK_CONST_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(canary, bool());
  MOCK_CONST_METHOD0(cluster, const ClusterInfo&());
  MOCK_CONST_METHOD0(outlierDetector, Outlier::DetectorHostSink&());
  MOCK_CONST_METHOD0(hostname, const std::string&());
  MOCK_CONST_METHOD0(stats, HostStats&());
  MOCK_CONST_METHOD0(zone, const std::string&());

  std::string hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  testing::NiceMock<Outlier::MockDetectorHostSink> outlier_detector_;
  testing::NiceMock<MockClusterInfo> cluster_;
  Stats::IsolatedStoreImpl stats_store_;
  HostStats stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_), POOL_GAUGE(stats_store_))};
};

class MockHost : public Host {
public:
  struct MockCreateConnectionData {
    Network::ClientConnection* connection_{};
    HostDescriptionConstSharedPtr host_description_{};
  };

  MockHost();
  ~MockHost();

  CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const override {
    MockCreateConnectionData data = createConnection_(dispatcher);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_description_};
  }

  void setOutlierDetector(Outlier::DetectorHostSinkPtr&& outlier_detector) override {
    setOutlierDetector_(outlier_detector);
  }

  MOCK_CONST_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(canary, bool());
  MOCK_CONST_METHOD0(cluster, const ClusterInfo&());
  MOCK_CONST_METHOD0(counters, std::list<Stats::CounterSharedPtr>());
  MOCK_CONST_METHOD1(createConnection_, MockCreateConnectionData(Event::Dispatcher& dispatcher));
  MOCK_CONST_METHOD0(gauges, std::list<Stats::GaugeSharedPtr>());
  MOCK_METHOD1(healthFlagClear, void(HealthFlag flag));
  MOCK_CONST_METHOD1(healthFlagGet, bool(HealthFlag flag));
  MOCK_METHOD1(healthFlagSet, void(HealthFlag flag));
  MOCK_CONST_METHOD0(healthy, bool());
  MOCK_CONST_METHOD0(hostname, const std::string&());
  MOCK_CONST_METHOD0(outlierDetector, Outlier::DetectorHostSink&());
  MOCK_METHOD1(setOutlierDetector_, void(Outlier::DetectorHostSinkPtr& outlier_detector));
  MOCK_CONST_METHOD0(stats, HostStats&());
  MOCK_CONST_METHOD0(weight, uint32_t());
  MOCK_METHOD1(weight, void(uint32_t new_weight));
  MOCK_CONST_METHOD0(zone, const std::string&());

  testing::NiceMock<MockClusterInfo> cluster_;
  testing::NiceMock<Outlier::MockDetectorHostSink> outlier_detector_;
  Stats::IsolatedStoreImpl stats_store_;
  HostStats stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_), POOL_GAUGE(stats_store_))};
};

} // namespace Upstream
} // namespace Envoy
