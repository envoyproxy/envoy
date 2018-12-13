#include <chrono>

#include "envoy/stats/stats.h"

#include "common/http/header_map_impl.h"
#include "common/router/retry_state_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Router {

class RouterRetryStateImplTest : public testing::Test {
public:
  RouterRetryStateImplTest() : callback_([this]() -> void { callback_ready_.ready(); }) {
    ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
        .WillByDefault(Return(true));
  }

  void setup() {
    Http::TestHeaderMapImpl headers;
    setup(headers);
  }

  void setup(Http::HeaderMap& request_headers) {
    state_ = RetryStateImpl::create(policy_, request_headers, cluster_, runtime_, random_,
                                    dispatcher_, Upstream::ResourcePriority::Default);
  }

  void expectTimerCreateAndEnable() {
    retry_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*retry_timer_, enableTimer(_));
  }

  NiceMock<TestRetryPolicy> policy_;
  NiceMock<Upstream::MockClusterInfo> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* retry_timer_{};
  RetryStatePtr state_;
  ReadyWatcher callback_ready_;
  RetryState::DoRetryCallback callback_;

  const absl::optional<Http::StreamResetReason> no_reset_;
  const absl::optional<Http::StreamResetReason> remote_reset_{Http::StreamResetReason::RemoteReset};
  const absl::optional<Http::StreamResetReason> remote_refused_stream_reset_{
      Http::StreamResetReason::RemoteRefusedStreamReset};
  const absl::optional<Http::StreamResetReason> overflow_reset_{Http::StreamResetReason::Overflow};
  const absl::optional<Http::StreamResetReason> connect_failure_{
      Http::StreamResetReason::ConnectionFailure};
};

TEST_F(RouterRetryStateImplTest, PolicyNoneRemoteReset) {
  Http::TestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_EQ(nullptr, state_);
}

TEST_F(RouterRetryStateImplTest, PolicyRefusedStream) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "refused-stream"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes,
            state_->shouldRetry(nullptr, remote_refused_stream_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_refused_stream_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxResetOverflow) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, overflow_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemoteReset) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, remote_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote503) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "503"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote503Overloaded) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "503"}, {"x-envoy-overloaded", "true"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyResourceExhaustedRemoteRateLimited) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "resource-exhausted"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"}, {"grpc-status", "8"}, {"x-envoy-ratelimited", "true"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemote502) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "502"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemote503) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "503"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemote504) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "504"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorResetOverflow) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, overflow_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemoteReset) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, remote_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcCancelled) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "cancelled"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "1"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcDeadlineExceeded) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "deadline-exceeded"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "4"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcResourceExhausted) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "resource-exhausted"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "8"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcUnavilable) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "unavailable"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "14"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcInternal) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "internal"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "13"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote200RemoteReset) {
  // Don't retry after reply start.
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RuntimeGuard) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillOnce(Return(false));

  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyConnectFailureOtherReset) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyConnectFailureResetConnectFailure) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxRetry) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "409"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxNoRetry) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestHeaderMapImpl response_headers{{":status", "400"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxReset) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RetriableStatusCodes) {
  policy_.retriable_status_codes_.push_back(409);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();

  Http::TestHeaderMapImpl response_headers{{":status", "409"}};
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RetriableStatusCodesUpstreamReset) {
  policy_.retriable_status_codes_.push_back(409);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RetriableStatusCodesHeader) {
  {
    Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"},
                                            {"x-envoy-retriable-status-codes", "200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  }
  {
    Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"},
                                            {"x-envoy-retriable-status-codes", "418,200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  }
  {
    Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"},
                                            {"x-envoy-retriable-status-codes", "   418 junk,200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(&response_headers, no_reset_, callback_));
  }
  {
    Http::TestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-status-codes"},
        {"x-envoy-retriable-status-codes", "   418 junk,xxx200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    Http::TestHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));
  }
}

TEST_F(RouterRetryStateImplTest, RouteConfigNoHeaderConfig) {
  policy_.num_retries_ = 1;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  Http::TestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();
}

TEST_F(RouterRetryStateImplTest, NoAvailableRetries) {
  cluster_.resetResourceManager(0, 0, 0, 0);

  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_overflow_.value());
}

TEST_F(RouterRetryStateImplTest, MaxRetriesHeader) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"},
                                          {"x-envoy-retry-grpc-on", "cancelled"},
                                          {"x-envoy-max-retries", "3"}};
  setup(request_headers);
  EXPECT_FALSE(request_headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-retry-grpc-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-max-retries"));
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(*retry_timer_, enableTimer(_));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(*retry_timer_, enableTimer(_));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_EQ(1UL, cluster_.circuit_breakers_stats_.rq_retry_open_.value());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(nullptr, connect_failure_, callback_));

  EXPECT_EQ(3UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(0UL, cluster_.stats().upstream_rq_retry_success_.value());
}

TEST_F(RouterRetryStateImplTest, Backoff) {
  policy_.num_retries_ = 3;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  Http::TestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(random_, random()).WillOnce(Return(49));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(24)));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(random_, random()).WillOnce(Return(149));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(74)));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  EXPECT_CALL(random_, random()).WillOnce(Return(349));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(174)));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->callback_();

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetry(&response_headers, no_reset_, callback_));

  EXPECT_EQ(3UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_success_.value());
  EXPECT_EQ(0UL, cluster_.circuit_breakers_stats_.rq_retry_open_.value());
}

TEST_F(RouterRetryStateImplTest, HostSelectionAttempts) {
  policy_.host_selection_max_attempts_ = 2;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;

  setup();

  EXPECT_EQ(2, state_->hostSelectionMaxAttempts());
}

TEST_F(RouterRetryStateImplTest, Cancel) {
  // Cover the case where we start a retry, and then we get destructed. This is how the router
  // uses the implementation in the cancel case.
  Http::TestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetry(nullptr, connect_failure_, callback_));
}

} // namespace Router
} // namespace Envoy
