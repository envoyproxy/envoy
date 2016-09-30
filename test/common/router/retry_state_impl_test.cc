#include "common/http/header_map_impl.h"
#include "common/router/retry_state_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Router {

class RouterRetryStateImplTest : public testing::Test {
public:
  RouterRetryStateImplTest() : callback_([this]() -> void { callback_ready_.ready(); }) {
    ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
        .WillByDefault(Return(true));
  }

  void setup(Http::HeaderMap& request_headers) {
    state_.reset(new RetryStateImpl{policy_, request_headers, cluster_, runtime_, random_,
                                    dispatcher_, Upstream::ResourcePriority::Default});
  }

  void expectTimerCreateAndEnable() {
    retry_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*retry_timer_, enableTimer(_));
  }

  TestRetryPolicy policy_;
  NiceMock<Upstream::MockCluster> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* retry_timer_{};
  RetryStatePtr state_;
  ReadyWatcher callback_ready_;
  RetryState::DoRetryCallback callback_;

  const Optional<Http::StreamResetReason> no_reset_;
  const Optional<Http::StreamResetReason> remote_reset_{Http::StreamResetReason::RemoteReset};
  const Optional<Http::StreamResetReason> remote_refused_stream_reset_{
      Http::StreamResetReason::RemoteRefusedStreamReset};
  const Optional<Http::StreamResetReason> connect_failure_{
      Http::StreamResetReason::ConnectionFailure};
};

TEST_F(RouterRetryStateImplTest, PolicyNoneRemoteReset) {
  Http::HeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_FALSE(state_->enabled());
  EXPECT_FALSE(state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyRefusedStream) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "refused-stream"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(nullptr, remote_refused_stream_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_FALSE(state_->shouldRetry(nullptr, remote_refused_stream_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemoteReset) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(nullptr, remote_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_FALSE(state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote503) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::HeaderMapImpl response_headers{{":status", "503"}};
  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_FALSE(state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote200RemoteReset) {
  // Don't retry after reply start.
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  Http::HeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_FALSE(state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_FALSE(state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RuntimeGuard) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillOnce(Return(false));

  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_FALSE(state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyConnectFailureOtherReset) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_FALSE(state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyConnectFailureResetConnectFailure) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxRetry) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::HeaderMapImpl response_headers{{":status", "409"}};
  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxNoRetry) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::HeaderMapImpl response_headers{{":status", "400"}};
  EXPECT_FALSE(state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxReset) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_FALSE(state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RouteConfigNoHeaderConfig) {
  policy_.num_retries_ = 1;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  Http::HeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();
}

TEST_F(RouterRetryStateImplTest, NoAvailableRetries) {
  cluster_.resource_manager_.reset(
      new Upstream::ResourceManagerImpl(runtime_, "fake_key", 0, 0, 0, 0));
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_FALSE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_overflow_.value());
}

TEST_F(RouterRetryStateImplTest, MaxRetriesHeader) {
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"},
                                      {"x-envoy-max-retries", "3"}};
  setup(request_headers);
  EXPECT_FALSE(request_headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-max-retries"));
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(*retry_timer_, enableTimer(_));
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(*retry_timer_, enableTimer(_));
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_FALSE(state_->shouldRetry(nullptr, connect_failure_, callback_));

  EXPECT_EQ(3UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(0UL, cluster_.stats().upstream_rq_retry_success_.value());
}

TEST_F(RouterRetryStateImplTest, Backoff) {
  policy_.num_retries_ = 3;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  Http::HeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(random_, random()).WillOnce(Return(49));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(24)));
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(random_, random()).WillOnce(Return(149));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(74)));
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(random_, random()).WillOnce(Return(349));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(174)));
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  Http::HeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_FALSE(state_->shouldRetry(&response_headers, no_reset_, callback_));

  EXPECT_EQ(3UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_success_.value());
}

TEST_F(RouterRetryStateImplTest, Cancel) {
  // Cover the case where we start a retry, and then we get destructed. This is how the router
  // uses the implementation in the cancel case.
  Http::HeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_TRUE(state_->shouldRetry(nullptr, connect_failure_, callback_));
}

} // Router
