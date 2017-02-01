#pragma once

#include "cluster_info.h"

#include "envoy/upstream/upstream.h"

#include "common/stats/stats_impl.h"

namespace Upstream {
namespace Outlier {

class MockDetectorHostSink : public DetectorHostSink {
public:
  MockDetectorHostSink();
  ~MockDetectorHostSink();

  MOCK_METHOD0(numEjections, uint32_t());
  MOCK_METHOD1(putHttpResponseCode, void(uint64_t code));
  MOCK_METHOD1(putResponseTime, void(std::chrono::milliseconds time));
};

class MockEventLogger : public EventLogger {
public:
  MockEventLogger();
  ~MockEventLogger();

  MOCK_METHOD2(logEject, void(HostDescriptionPtr host, EjectionType type));
  MOCK_METHOD1(logUneject, void(HostDescriptionPtr host));
};

class MockDetector : public Detector {
public:
  MockDetector();
  ~MockDetector();

  void runCallbacks(HostPtr host) {
    for (ChangeStateCb cb : callbacks_) {
      cb(host);
    }
  }

  MOCK_METHOD1(addChangedStateCb, void(ChangeStateCb cb));

  std::list<ChangeStateCb> callbacks_;
};

} // Outlier

class MockHostDescription : public HostDescription {
public:
  MockHostDescription();
  ~MockHostDescription();

  MOCK_CONST_METHOD0(canary, bool());
  MOCK_CONST_METHOD0(cluster, const ClusterInfo&());
  MOCK_CONST_METHOD0(outlierDetector, Outlier::DetectorHostSink&());
  MOCK_CONST_METHOD0(url, const std::string&());
  MOCK_CONST_METHOD0(stats, HostStats&());
  MOCK_CONST_METHOD0(zone, const std::string&());

  std::string url_{"tcp://10.0.0.1:443"};
  testing::NiceMock<Outlier::MockDetectorHostSink> outlier_detector_;
  testing::NiceMock<MockClusterInfo> cluster_;
  Stats::IsolatedStoreImpl stats_store_;
  HostStats stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_), POOL_GAUGE(stats_store_))};
};

class MockHost : public Host {
public:
  struct MockCreateConnectionData {
    Network::ClientConnection* connection_{};
    ConstHostPtr host_;
  };

  MockHost();
  ~MockHost();

  MOCK_CONST_METHOD0(cluster, const ClusterInfo&());
  MOCK_CONST_METHOD0(url, const std::string&());
  MOCK_CONST_METHOD0(counters, std::list<Stats::CounterPtr>());
  MOCK_CONST_METHOD0(gauges, std::list<Stats::GaugePtr>());
  MOCK_CONST_METHOD0(healthy, bool());
  MOCK_METHOD1(healthy, void(bool));
  MOCK_CONST_METHOD0(stats, HostStats&());
  MOCK_CONST_METHOD0(weight, uint32_t());
  MOCK_METHOD1(weight, void(uint32_t new_weight));

  CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const override {
    MockCreateConnectionData data = createConnection_(dispatcher);
    return {Network::ClientConnectionPtr{data.connection_}, data.host_};
  }

  MOCK_CONST_METHOD1(createConnection_, MockCreateConnectionData(Event::Dispatcher& dispatcher));
};

} // Upstream
