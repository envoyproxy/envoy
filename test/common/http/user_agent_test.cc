#include "common/http/header_map_impl.h"
#include "common/http/user_agent.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Property;
using testing::_;

namespace Envoy {
namespace Http {

TEST(UserAgentTest, All) {
  Stats::MockStore stat_store;
  NiceMock<Stats::MockHistogram> original_histogram;
  Stats::Timespan span(original_histogram);

  EXPECT_CALL(stat_store.counter_, inc()).Times(5);
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_cx_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_rq_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_cx_destroy_remote_active_rq"));
  EXPECT_CALL(stat_store, histogram("test.user_agent.ios.downstream_cx_length_ms"));
  EXPECT_CALL(
      stat_store,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "test.user_agent.ios.downstream_cx_length_ms"), _));

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa iOS bbb"}}, "test.", stat_store);
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, "test.",
                             stat_store);
    ua.completeConnectionLength(span);
  }

  EXPECT_CALL(stat_store, counter("test.user_agent.android.downstream_cx_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.android.downstream_rq_total"));
  EXPECT_CALL(stat_store,
              counter("test.user_agent.android.downstream_cx_destroy_remote_active_rq"));
  EXPECT_CALL(stat_store, histogram("test.user_agent.android.downstream_cx_length_ms"));
  EXPECT_CALL(
      stat_store,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "test.user_agent.android.downstream_cx_length_ms"), _));

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, "test.",
                             stat_store);
    ua.completeConnectionLength(span);
    ua.onConnectionDestroy(Network::ConnectionEvent::RemoteClose, true);
  }

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa bbb"}}, "test.", stat_store);
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, "test.",
                             stat_store);
    ua.completeConnectionLength(span);
    ua.onConnectionDestroy(Network::ConnectionEvent::RemoteClose, false);
  }

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{}, "test.", stat_store);
    ua.completeConnectionLength(span);
  }
}

} // namespace Http
} // namespace Envoy
