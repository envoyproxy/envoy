#include "source/common/config/well_known_names.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/user_agent.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {
namespace {

// The per-device stats are created against a real store so that their flat name, the name they are
// tag-extracted to, and the tags attached to them are all observable. The device is carried by an
// explicit 'envoy.http_user_agent' tag rather than being recovered from the stat name.
void expectDeviceStat(Stats::TestUtil::TestStore& store, const std::string& name,
                      const std::string& tag_extracted_name, const std::string& device,
                      uint64_t value) {
  Stats::CounterOptConstRef counter = store.findCounterByString(name);
  ASSERT_TRUE(counter.has_value()) << "no counter named '" << name << "'";
  EXPECT_EQ(counter->get().value(), value) << " for stat '" << name << "'";
  EXPECT_EQ(counter->get().tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";

  using TagVector = std::vector<std::pair<std::string, std::string>>;
  TagVector tags;
  for (const Stats::Tag& tag : counter->get().tags()) {
    tags.emplace_back(tag.name_, tag.value_);
  }
  const TagVector expected_tags{{Config::TagNames::get().HTTP_USER_AGENT, device}};
  EXPECT_EQ(tags, expected_tags) << " for stat '" << name << "'";
}

TEST(UserAgentTest, All) {
  Stats::TestUtil::TestStore store;
  Stats::Scope& scope = *store.rootScope();
  NiceMock<Stats::MockHistogram> original_histogram;
  original_histogram.unit_ = Stats::Histogram::Unit::Milliseconds;
  Event::SimulatedTimeSystem time_system;
  Stats::HistogramCompletableTimespanImpl span(original_histogram, time_system);

  UserAgentContext context(store.symbolTable());

  // The user agent is latched from the first request, so the second, Android request is counted
  // against the iOS stats.
  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa iOS bbb"}}, scope);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, scope);
    ua.completeConnectionLength(span);
  }

  expectDeviceStat(store, "user_agent.ios.downstream_cx_total", "user_agent.downstream_cx_total",
                   "ios", 1);
  expectDeviceStat(store, "user_agent.ios.downstream_rq_total", "user_agent.downstream_rq_total",
                   "ios", 2);
  EXPECT_TRUE(store.histogramRecordedValues("user_agent.ios.downstream_cx_length_ms"));

  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, scope);
    ua.completeConnectionLength(span);
    ua.onConnectionDestroy(Network::ConnectionEvent::RemoteClose, true);
  }

  expectDeviceStat(store, "user_agent.android.downstream_cx_total",
                   "user_agent.downstream_cx_total", "android", 1);
  expectDeviceStat(store, "user_agent.android.downstream_rq_total",
                   "user_agent.downstream_rq_total", "android", 1);
  expectDeviceStat(store, "user_agent.android.downstream_cx_destroy_remote_active_rq",
                   "user_agent.downstream_cx_destroy_remote_active_rq", "android", 1);
  EXPECT_TRUE(store.histogramRecordedValues("user_agent.android.downstream_cx_length_ms"));

  // A request with no recognized device, then an Android one: the user agent is latched on the
  // first request, so no device stats are created at all. Destroying the connection without active
  // streams records nothing either.
  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa bbb"}}, scope);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, scope);
    ua.completeConnectionLength(span);
    ua.onConnectionDestroy(Network::ConnectionEvent::RemoteClose, false);
  }

  EXPECT_EQ(1, store.counter("user_agent.android.downstream_cx_total").value());
  EXPECT_EQ(1, store.counter("user_agent.android.downstream_cx_destroy_remote_active_rq").value());

  // No user agent header at all.
  {
    UserAgent ua(context);
    ua.initializeFromHeaders(TestRequestHeaderMapImpl{}, scope);
    ua.completeConnectionLength(span);
  }

  EXPECT_EQ(1, store.counter("user_agent.ios.downstream_cx_total").value());
  EXPECT_EQ(1, store.counter("user_agent.android.downstream_cx_total").value());
}

} // namespace
} // namespace Http
} // namespace Envoy
