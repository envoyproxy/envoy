#include "common/http/header_map_impl.h"
#include "common/http/user_agent.h"

#include "test/mocks/common.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Property;

namespace Envoy {
namespace Http {
namespace {

TEST(UserAgentTest, All) {
  Stats::MockStore stat_store;
  NiceMock<Stats::MockHistogram> original_histogram;
  original_histogram.unit_ = Stats::Histogram::Unit::Milliseconds;
  Event::SimulatedTimeSystem time_system;
  Stats::HistogramCompletableTimespanImpl span(original_histogram, time_system);

  EXPECT_CALL(stat_store.counter_, inc()).Times(5);
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_cx_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_rq_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_cx_destroy_remote_active_rq"));
  EXPECT_CALL(stat_store, histogram("test.user_agent.ios.downstream_cx_length_ms",
                                    Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(
      stat_store,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "test.user_agent.ios.downstream_cx_length_ms"), _));

  UserAgentContext context(stat_store.symbolTable());
  Stats::StatNamePool pool(stat_store.symbolTable());
  Stats::StatName prefix = pool.add("test");
  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa iOS bbb"}}, prefix,
                             stat_store);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, prefix,
                             stat_store);
    ua.completeConnectionLength(span);
  }

  EXPECT_CALL(stat_store, counter("test.user_agent.android.downstream_cx_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.android.downstream_rq_total"));
  EXPECT_CALL(stat_store,
              counter("test.user_agent.android.downstream_cx_destroy_remote_active_rq"));
  EXPECT_CALL(stat_store, histogram("test.user_agent.android.downstream_cx_length_ms",
                                    Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(
      stat_store,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "test.user_agent.android.downstream_cx_length_ms"), _));

  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, prefix,
                             stat_store);
    ua.completeConnectionLength(span);
    ua.onConnectionDestroy(Network::ConnectionEvent::RemoteClose, true);
  }

  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa bbb"}}, prefix,
                             stat_store);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, prefix,
                             stat_store);
    ua.completeConnectionLength(span);
    ua.onConnectionDestroy(Network::ConnectionEvent::RemoteClose, false);
  }

  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{}, prefix, stat_store);
    ua.completeConnectionLength(span);
  }
}

} // namespace
} // namespace Http
} // namespace Envoy
