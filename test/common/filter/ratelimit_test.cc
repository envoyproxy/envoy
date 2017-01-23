#include "common/buffer/buffer_impl.h"
#include "common/filter/ratelimit.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace RateLimit {
namespace TcpFilter {

class RateLimitFilterTest : public testing::Test {
public:
  RateLimitFilterTest() {
    std::string json = R"EOF(
    {
      "domain": "foo",
      "descriptors": [
         [{"key": "hello", "value": "world"}, {"key": "foo", "value": "bar"}],
         [{"key": "foo2", "value": "bar2"}]
       ],
       "stat_prefix": "name"
    }
    )EOF";

    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enforcing", 100))
        .WillByDefault(Return(true));

    Json::ObjectPtr config = Json::Factory::LoadFromString(json);
    config_.reset(new Config(*config, stats_store_, runtime_));
    client_ = new MockClient();
    filter_.reset(new Instance(config_, ClientPtr{client_}));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  ~RateLimitFilterTest() {
    for (Stats::GaugePtr gauge : stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  ConfigPtr config_;
  MockClient* client_;
  std::unique_ptr<Instance> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  RequestCallbacks* request_callbacks_{};
};

TEST_F(RateLimitFilterTest, OK) {
  InSequence s;

  EXPECT_CALL(*client_,
              limit(_, "foo", testing::ContainerEq(std::vector<Descriptor>{
                                  {{{"hello", "world"}, {"foo", "bar"}}}, {{{"foo2", "bar2"}}}}),
                    ""))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(LimitStatus::OK);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, OverLimit) {
  InSequence s;

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*client_, cancel()).Times(0);
  request_callbacks_->complete(LimitStatus::OverLimit);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.cx_closed").value());
}

TEST_F(RateLimitFilterTest, OverLimitNotEnforcing) {
  InSequence s;

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enforcing", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(*client_, cancel()).Times(0);
  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(LimitStatus::OverLimit);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.over_limit").value());
  EXPECT_EQ(0U, stats_store_.counter("ratelimit.name.cx_closed").value());
}

TEST_F(RateLimitFilterTest, Error) {
  InSequence s;

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(LimitStatus::Error);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.error").value());
}

TEST_F(RateLimitFilterTest, Disconnect) {
  InSequence s;

  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(WithArgs<0>(
          Invoke([&](RequestCallbacks& callbacks) -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
}

TEST_F(RateLimitFilterTest, ImmediateOK) {
  InSequence s;

  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, limit(_, "foo", _, _))
      .WillOnce(WithArgs<0>(Invoke([&](RequestCallbacks& callbacks)
                                       -> void { callbacks.complete(LimitStatus::OK); })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, RuntimeDisable) {
  InSequence s;

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data));
}

} // TcpFilter
} // RateLimit
