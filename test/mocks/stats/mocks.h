#pragma once

#include "envoy/stats/stats.h"

#include "common/stats/stats_impl.h"

namespace Stats {

class MockCounter : public Counter {
public:
  MockCounter();
  ~MockCounter();

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(inc, void());
  MOCK_METHOD0(latch, uint64_t());
  MOCK_METHOD0(name, std::string());
  MOCK_METHOD0(reset, void());
  MOCK_METHOD0(used, bool());
  MOCK_METHOD0(value, uint64_t());
};

class MockTimespan : public Timespan {
public:
  MockTimespan();
  ~MockTimespan();

  MOCK_METHOD0(complete, void());
  MOCK_METHOD1(complete, void(const std::string& dynamic_name));
};

class MockStore : public Store {
public:
  MockStore();
  ~MockStore();

  MOCK_METHOD1(addSink, void(Sink&));
  MOCK_METHOD2(deliverHistogramToSinks, void(const std::string& name, uint64_t value));
  MOCK_METHOD2(deliverTimingToSinks, void(const std::string&, std::chrono::milliseconds));
  MOCK_METHOD1(counter, Counter&(const std::string&));
  MOCK_CONST_METHOD0(counters, std::list<std::reference_wrapper<Counter>>());
  MOCK_METHOD1(gauge, Gauge&(const std::string&));
  MOCK_CONST_METHOD0(gauges, std::list<std::reference_wrapper<Gauge>>());
  MOCK_METHOD1(timer, Timer&(const std::string& name));

  testing::NiceMock<MockCounter> counter_;
};

/**
 * With IsolatedStoreImpl it's hard to test timing stats.
 * MockIsolatedStatsStore mocks only deliverTimingToSinks for better testing.
 */
class MockIsolatedStatsStore : public IsolatedStoreImpl {
public:
  MockIsolatedStatsStore();
  ~MockIsolatedStatsStore();

  MOCK_METHOD2(deliverTimingToSinks, void(const std::string&, std::chrono::milliseconds));
};

} // Stats
