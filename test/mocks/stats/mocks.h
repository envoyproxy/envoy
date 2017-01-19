#pragma once

#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"

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

class MockGauge : public Gauge {
public:
  MockGauge();
  ~MockGauge();

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(dec, void());
  MOCK_METHOD0(inc, void());
  MOCK_METHOD0(name, std::string());
  MOCK_METHOD1(set, void(uint64_t value));
  MOCK_METHOD1(sub, void(uint64_t amount));
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

class MockSink : public Sink {
public:
  MockSink();
  ~MockSink();

  MOCK_METHOD2(flushCounter, void(const std::string& name, uint64_t delta));
  MOCK_METHOD2(flushGauge, void(const std::string& name, uint64_t value));
  MOCK_METHOD2(onHistogramComplete, void(const std::string& name, uint64_t value));
  MOCK_METHOD2(onTimespanComplete, void(const std::string& name, std::chrono::milliseconds ms));
};

class MockStore : public Store {
public:
  MockStore();
  ~MockStore();

  ScopePtr createScope(const std::string& name) override { return ScopePtr{createScope_(name)}; }

  MOCK_METHOD2(deliverHistogramToSinks, void(const std::string& name, uint64_t value));
  MOCK_METHOD2(deliverTimingToSinks, void(const std::string&, std::chrono::milliseconds));
  MOCK_METHOD1(counter, Counter&(const std::string&));
  MOCK_CONST_METHOD0(counters, std::list<CounterPtr>());
  MOCK_METHOD1(createScope_, Scope*(const std::string& name));
  MOCK_METHOD1(gauge, Gauge&(const std::string&));
  MOCK_CONST_METHOD0(gauges, std::list<GaugePtr>());
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
