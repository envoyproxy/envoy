#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/stats/stats.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/ratelimit/ratelimit.h"

#include "test/extensions/filters/common/ratelimit/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

class RateLimitFilterTest : public testing::Test {
public:
  void SetUpTest(const std::string& yaml) {
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enforcing", 100))
        .WillByDefault(Return(true));

    envoy::extensions::filters::network::ratelimit::v3::RateLimit proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);
    config_.reset(new Config(proto_config, stats_store_, runtime_));
    client_ = new Filters::Common::RateLimit::MockClient();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::RateLimit::ClientPtr{client_});
    filter_->initializeReadFilterCallbacks(filter_callbacks_);

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  ~RateLimitFilterTest() override {
    for (const Stats::GaugeSharedPtr& gauge : stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  const std::string filter_config_ = R"EOF(
domain: foo
descriptors:
- entries:
   - key: hello
     value: world
   - key: foo
     value: bar
- entries:
   - key: foo2
     value: bar2
stat_prefix: name
)EOF";

  const std::string fail_close_config_ = R"EOF(
domain: foo
descriptors:
- entries:
   - key: hello
     value: world
   - key: foo
     value: bar
- entries:
   - key: foo2
     value: bar2
stat_prefix: name
failure_mode_deny: true
)EOF";

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  ConfigSharedPtr config_;
  Filters::Common::RateLimit::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  Filters::Common::RateLimit::RequestCallbacks* request_callbacks_{};
};

TEST_F(RateLimitFilterTest, OK) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<RateLimit::Descriptor>{
                                  {{{"hello", "world"}, {"foo", "bar"}}}, {{{"foo2", "bar2"}}}}),
                              testing::A<Tracing::Span&>()))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, OverLimit) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*client_, cancel()).Times(0);
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr,
                               nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.cx_closed").value());
}

TEST_F(RateLimitFilterTest, OverLimitNotEnforcing) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enforcing", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(*client_, cancel()).Times(0);
  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr,
                               nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.over_limit").value());
  EXPECT_EQ(0U, stats_store_.counter("ratelimit.name.cx_closed").value());
}

TEST_F(RateLimitFilterTest, Error) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.error").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.failure_mode_allowed").value());
}

TEST_F(RateLimitFilterTest, Disconnect) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
}

TEST_F(RateLimitFilterTest, ImmediateOK) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            callbacks.complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr);
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, ImmediateError) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            callbacks.complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr);
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.error").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.failure_mode_allowed").value());
}

TEST_F(RateLimitFilterTest, RuntimeDisable) {
  InSequence s;
  SetUpTest(filter_config_);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
}

TEST_F(RateLimitFilterTest, ErrorResponseWithFailureModeAllowOff) {
  InSequence s;
  SetUpTest(fail_close_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ratelimit.name.failure_mode_allowed").value());
}

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
