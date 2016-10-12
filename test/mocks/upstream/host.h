#pragma once

#include "envoy/upstream/upstream.h"

namespace Upstream {

class MockOutlierDetectorHostSink : public OutlierDetectorHostSink {
public:
  MockOutlierDetectorHostSink();
  ~MockOutlierDetectorHostSink();

  MOCK_METHOD1(putHttpResponseCode, void(uint64_t code));
  MOCK_METHOD1(putResponseTime, void(std::chrono::milliseconds time));
};

class MockOutlierDetector : public OutlierDetector {
public:
  MockOutlierDetector();
  ~MockOutlierDetector();

  void runCallbacks(HostPtr host) {
    for (ChangeStateCb cb : callbacks_) {
      cb(host);
    }
  }

  MOCK_METHOD1(addChangedStateCb, void(ChangeStateCb cb));

  std::list<ChangeStateCb> callbacks_;
};

class MockHostDescription : public HostDescription {
public:
  MockHostDescription();
  ~MockHostDescription();

  MOCK_CONST_METHOD0(canary, bool());
  MOCK_CONST_METHOD0(cluster, const Cluster&());
  MOCK_CONST_METHOD0(outlierDetector, OutlierDetectorHostSink&());
  MOCK_CONST_METHOD0(url, const std::string&());
  MOCK_CONST_METHOD0(stats, HostStats&());
  MOCK_CONST_METHOD0(zone, const std::string&());

  std::string url_{"tcp://10.0.0.1:443"};
  testing::NiceMock<MockOutlierDetectorHostSink> outlier_detector_;
};

class MockHost : public Host {
public:
  struct MockCreateConnectionData {
    Network::ClientConnection* connection_{};
    ConstHostPtr host_;
  };

  MockHost();
  ~MockHost();

  MOCK_CONST_METHOD0(cluster, const Cluster&());
  MOCK_CONST_METHOD0(url, const std::string&());
  MOCK_CONST_METHOD0(counters, std::list<std::reference_wrapper<Stats::Counter>>());
  MOCK_CONST_METHOD0(gauges, std::list<std::reference_wrapper<Stats::Gauge>>());
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
