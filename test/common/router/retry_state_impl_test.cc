#include <chrono>

#include "envoy/config/route/v3/route_components.pb.h"
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
namespace {

class RouterRetryStateImplTest : public testing::Test {
public:
  enum TestResourceType { Connection, Request, PendingRequest, Retry };

  RouterRetryStateImplTest() : callback_([this]() -> void { callback_ready_.ready(); }) {
    ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
        .WillByDefault(Return(true));
  }

  void setup() {
    Http::TestRequestHeaderMapImpl headers;
    setup(headers);
  }

  void setup(Http::RequestHeaderMap& request_headers) {
    state_ = RetryStateImpl::create(policy_, request_headers, cluster_, &virtual_cluster_, runtime_,
                                    random_, dispatcher_, Upstream::ResourcePriority::Default);
  }

  void expectTimerCreateAndEnable() {
    retry_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  }

  void incrOutstandingResource(TestResourceType resource, uint32_t num) {
    for (uint32_t i = 0; i < num; ++i) {
      switch (resource) {
      case TestResourceType::Retry:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).retries().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).retries().dec();
        });
        break;
      case TestResourceType::Connection:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).connections().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).connections().dec();
        });
        break;
      case TestResourceType::Request:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).requests().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).requests().dec();
        });
        break;
      case TestResourceType::PendingRequest:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).pendingRequests().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).pendingRequests().dec();
        });
        break;
      }
    }
  }

  void cleanupOutstandingResources() {
    for (auto& task : resource_manager_cleanup_tasks_) {
      task();
    }
    resource_manager_cleanup_tasks_.clear();
  }

  void verifyPolicyWithRemoteResponse(const std::string& retry_on,
                                      const std::string& response_status, const bool is_grpc) {
    Http::TestRequestHeaderMapImpl request_headers;
    if (is_grpc) {
      request_headers.setEnvoyRetryGrpcOn(retry_on);
    } else {
      request_headers.setEnvoyRetryOn(retry_on);
    }
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    Http::TestResponseHeaderMapImpl response_headers;
    if (is_grpc) {
      response_headers.setStatus("200");
      response_headers.setGrpcStatus(response_status);
    } else {
      response_headers.setStatus(response_status);
    }

    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
    EXPECT_CALL(callback_ready_, ready());
    retry_timer_->invokeCallback();

    EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
              state_->shouldRetryHeaders(response_headers, callback_));

    EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
    EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
    EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_.value());
    EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_.value());
  }

  void TearDown() override { cleanupOutstandingResources(); }

  NiceMock<TestRetryPolicy> policy_;
  NiceMock<Upstream::MockClusterInfo> cluster_;
  TestVirtualCluster virtual_cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* retry_timer_{};
  RetryStatePtr state_;
  ReadyWatcher callback_ready_;
  RetryState::DoRetryCallback callback_;
  std::vector<std::function<void()>> resource_manager_cleanup_tasks_;

  const Http::StreamResetReason remote_reset_{Http::StreamResetReason::RemoteReset};
  const Http::StreamResetReason remote_refused_stream_reset_{
      Http::StreamResetReason::RemoteRefusedStreamReset};
  const Http::StreamResetReason overflow_reset_{Http::StreamResetReason::Overflow};
  const Http::StreamResetReason connect_failure_{Http::StreamResetReason::ConnectionFailure};
};

TEST_F(RouterRetryStateImplTest, PolicyNoneRemoteReset) {
  Http::TestRequestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_EQ(nullptr, state_);
}

TEST_F(RouterRetryStateImplTest, PolicyRefusedStream) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "refused-stream"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(remote_refused_stream_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
            state_->shouldRetryReset(remote_refused_stream_reset_, callback_));

  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

TEST_F(RouterRetryStateImplTest, Policy5xxResetOverflow) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(overflow_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemoteReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(remote_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded, state_->shouldRetryReset(remote_reset_, callback_));

  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote503) {
  verifyPolicyWithRemoteResponse("5xx" /* retry_on */, "503" /* response_status */,
                                 false /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote503Overloaded) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"},
                                                   {"x-envoy-overloaded", "true"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyResourceExhaustedRemoteRateLimited) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "resource-exhausted"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"grpc-status", "8"}, {"x-envoy-ratelimited", "true"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemote502) {
  verifyPolicyWithRemoteResponse("gateway-error" /* retry_on */, "502" /* response_status */,
                                 false /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemote503) {
  verifyPolicyWithRemoteResponse("gateway-error" /* retry_on */, "503" /* response_status */,
                                 false /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemote504) {
  verifyPolicyWithRemoteResponse("gateway-error" /* retry_on */, "504" /* response_status */,
                                 false /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorResetOverflow) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(overflow_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyGatewayErrorRemoteReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(remote_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded, state_->shouldRetryReset(remote_reset_, callback_));

  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcCancelled) {
  verifyPolicyWithRemoteResponse("cancelled" /* retry_on */, "1" /* response_status */,
                                 true /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcDeadlineExceeded) {
  verifyPolicyWithRemoteResponse("deadline-exceeded" /* retry_on */, "4" /* response_status */,
                                 true /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcResourceExhausted) {
  verifyPolicyWithRemoteResponse("resource-exhausted" /* retry_on */, "8" /* response_status */,
                                 true /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcUnavilable) {
  verifyPolicyWithRemoteResponse("unavailable" /* retry_on */, "14" /* response_status */,
                                 true /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyGrpcInternal) {
  verifyPolicyWithRemoteResponse("internal" /* retry_on */, "13" /* response_status */,
                                 true /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, Policy5xxRemote200RemoteReset) {
  // Don't retry after reply start.
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(remote_reset_, callback_));
  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded, state_->shouldRetryReset(remote_reset_, callback_));

  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

TEST_F(RouterRetryStateImplTest, RuntimeGuard) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillOnce(Return(false));

  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyConnectFailureOtherReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyConnectFailureResetConnectFailure) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxRetry) {
  verifyPolicyWithRemoteResponse("retriable-4xx", "409", false /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxNoRetry) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "400"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyRetriable4xxReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RetriableStatusCodes) {
  policy_.retriable_status_codes_.push_back(409);
  verifyPolicyWithRemoteResponse("retriable-status-codes", "409", false /* is_grpc */);
}

TEST_F(RouterRetryStateImplTest, RetriableStatusCodesUpstreamReset) {
  policy_.retriable_status_codes_.push_back(409);
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, RetriableStatusCodesHeader) {
  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"},
                                                   {"x-envoy-retriable-status-codes", "200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"},
                                                   {"x-envoy-retriable-status-codes", "418,200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-status-codes"},
        {"x-envoy-retriable-status-codes", "   418 junk,200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-status-codes"},
        {"x-envoy-retriable-status-codes", "   418 junk,xxx200"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }
}

// Test that when 'retriable-headers' policy is set via request header, certain configured headers
// trigger retries.
TEST_F(RouterRetryStateImplTest, RetriableHeadersPolicySetViaRequestHeader) {
  policy_.retry_on_ = RetryPolicy::RETRY_ON_5XX;

  Protobuf::RepeatedPtrField<envoy::config::route::v3::HeaderMatcher> matchers;
  auto* matcher = matchers.Add();
  matcher->set_name("X-Upstream-Pushback");

  policy_.retriable_headers_ = Http::HeaderUtility::buildHeaderMatcherVector(matchers);

  // No retries based on response headers: retry mode isn't enabled.
  {
    Http::TestRequestHeaderMapImpl request_headers;
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "true"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }

  // Retries based on response headers: retry mode enabled via request header.
  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-headers"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());
    expectTimerCreateAndEnable();

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "true"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
}

// Test that when 'retriable-headers' policy is set via retry policy configuration,
// configured header matcher conditions trigger retries.
TEST_F(RouterRetryStateImplTest, RetriableHeadersPolicyViaRetryPolicyConfiguration) {
  policy_.retry_on_ = RetryPolicy::RETRY_ON_RETRIABLE_HEADERS;

  Protobuf::RepeatedPtrField<envoy::config::route::v3::HeaderMatcher> matchers;

  auto* matcher1 = matchers.Add();
  matcher1->set_name("X-Upstream-Pushback");

  auto* matcher2 = matchers.Add();
  matcher2->set_name("should-retry");
  matcher2->set_exact_match("yes");

  auto* matcher3 = matchers.Add();
  matcher3->set_name("X-Verdict");
  matcher3->set_prefix_match("retry");

  auto* matcher4 = matchers.Add();
  matcher4->set_name(":status");
  matcher4->mutable_range_match()->set_start(500);
  matcher4->mutable_range_match()->set_end(505);

  policy_.retriable_headers_ = Http::HeaderUtility::buildHeaderMatcherVector(matchers);

  auto setup_request = [this]() {
    Http::TestRequestHeaderMapImpl request_headers;
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());
  };

  // matcher1: header presence (any value).
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    expectTimerCreateAndEnable();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "true"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    expectTimerCreateAndEnable();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "false"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }

  // matcher2: exact header value match.
  {
    setup_request();
    expectTimerCreateAndEnable();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"should-retry", "yes"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"should-retry", "no"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }

  // matcher3: prefix match.
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-verdict", "retry-please"}};
    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-verdict", "dont-retry-please"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }

  // matcher4: status code range (note half-open semantics: [start, end)).
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "499"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "504"}};
    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    setup_request();
    Http::TestResponseHeaderMapImpl response_headers{{":status", "505"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }
}

// Test various combinations of retry headers set via request headers.
TEST_F(RouterRetryStateImplTest, RetriableHeadersSetViaRequestHeader) {
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-headers"},
        {"x-envoy-retriable-header-names", "X-Upstream-Pushback,FOOBAR"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestResponseHeaderMapImpl response_headers{{"x-upstream-pushback", "yes"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-headers"},
        {"x-envoy-retriable-header-names", "X-Upstream-Pushback,  FOOBAR  "}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestResponseHeaderMapImpl response_headers{{"foobar", "false"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-headers"},
        {"x-envoy-retriable-header-names", "X-Upstream-Pushback,,FOOBAR"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }
}

// Test merging retriable headers set via request headers and via config file.
TEST_F(RouterRetryStateImplTest, RetriableHeadersMergedConfigAndRequestHeaders) {
  policy_.retry_on_ = RetryPolicy::RETRY_ON_RETRIABLE_HEADERS;

  Protobuf::RepeatedPtrField<envoy::config::route::v3::HeaderMatcher> matchers;

  // Config says: retry if response is not 200.
  auto* matcher = matchers.Add();
  matcher->set_name(":status");
  matcher->set_exact_match("200");
  matcher->set_invert_match(true);

  policy_.retriable_headers_ = Http::HeaderUtility::buildHeaderMatcherVector(matchers);

  // No retries according to config.
  {
    Http::TestRequestHeaderMapImpl request_headers;
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }

  // Request header supplements the config: as a result we retry on 200.
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retriable-header-names", "  :status,  FOOBAR  "}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());

    expectTimerCreateAndEnable();

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }
}

TEST_F(RouterRetryStateImplTest, PolicyResetRemoteReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "reset"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(remote_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded, state_->shouldRetryReset(remote_reset_, callback_));

  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

TEST_F(RouterRetryStateImplTest, PolicyLimitedByRequestHeaders) {
  Protobuf::RepeatedPtrField<envoy::config::route::v3::HeaderMatcher> matchers;
  auto* matcher = matchers.Add();
  matcher->set_name(":method");
  matcher->set_exact_match("GET");

  auto* matcher2 = matchers.Add();
  matcher2->set_name(":method");
  matcher2->set_exact_match("HEAD");

  policy_.retriable_request_headers_ = Http::HeaderUtility::buildHeaderMatcherVector(matchers);

  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
    setup(request_headers);
    EXPECT_FALSE(state_->enabled());
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {"x-envoy-retry-on", "retriable-4xx"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());
    Http::TestResponseHeaderMapImpl response_headers{{":status", "409"}};
    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {"x-envoy-retry-on", "5xx"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "HEAD"},
                                                   {"x-envoy-retry-on", "5xx"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    expectTimerCreateAndEnable();
    EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
  }

  // Sanity check that we're only enabling retries for the configured retry-on.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "HEAD"},
                                                   {"x-envoy-retry-on", "retriable-4xx"}};
    setup(request_headers);
    EXPECT_TRUE(state_->enabled());
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                   {"x-envoy-retry-on", "5xx"}};
    setup(request_headers);
    EXPECT_FALSE(state_->enabled());
  }
}

TEST_F(RouterRetryStateImplTest, RouteConfigNoRetriesAllowed) {
  policy_.num_retries_ = 0;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  setup();

  EXPECT_TRUE(state_->enabled());
  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
            state_->shouldRetryReset(connect_failure_, callback_));

  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(0UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(0UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

TEST_F(RouterRetryStateImplTest, RouteConfigNoHeaderConfig) {
  policy_.num_retries_ = 1;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  Http::TestRequestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();
}

TEST_F(RouterRetryStateImplTest, NoAvailableRetries) {
  cluster_.resetResourceManager(0, 0, 0, 0, 0);

  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_overflow_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_overflow_.value());
}

TEST_F(RouterRetryStateImplTest, MaxRetriesHeader) {
  // The max retries header will take precedence over the policy
  policy_.num_retries_ = 4;
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"},
                                                 {"x-envoy-retry-grpc-on", "cancelled"},
                                                 {"x-envoy-max-retries", "3"}};
  setup(request_headers);
  EXPECT_FALSE(request_headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-retry-grpc-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-max-retries"));
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_EQ(1UL, cluster_.circuit_breakers_stats_.rq_retry_open_.value());
  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
            state_->shouldRetryReset(connect_failure_, callback_));

  EXPECT_EQ(3UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(0UL, cluster_.stats().upstream_rq_retry_success_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(3UL, virtual_cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(0UL, virtual_cluster_.stats().upstream_rq_retry_success_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
}

TEST_F(RouterRetryStateImplTest, Backoff) {
  policy_.num_retries_ = 3;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  Http::TestRequestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(random_, random()).WillOnce(Return(190));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(15), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(190));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(40), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(190));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(90), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers, callback_));

  EXPECT_EQ(3UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_success_.value());
  EXPECT_EQ(3UL, virtual_cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_success_.value());
  EXPECT_EQ(0UL, cluster_.circuit_breakers_stats_.rq_retry_open_.value());
}

// Test customized retry back-off intervals.
TEST_F(RouterRetryStateImplTest, CustomBackOffInterval) {
  policy_.num_retries_ = 10;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  policy_.base_interval_ = std::chrono::milliseconds(100);
  policy_.max_interval_ = std::chrono::milliseconds(1200);
  Http::TestRequestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(random_, random()).WillOnce(Return(149));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(49), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(350));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(150), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(751));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(351), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(2399));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(799), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(2399));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1199), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();
}

// Test the default maximum retry back-off interval.
TEST_F(RouterRetryStateImplTest, CustomBackOffIntervalDefaultMax) {
  policy_.num_retries_ = 10;
  policy_.retry_on_ = RetryPolicy::RETRY_ON_CONNECT_FAILURE;
  policy_.base_interval_ = std::chrono::milliseconds(100);
  Http::TestRequestHeaderMapImpl request_headers;
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(random_, random()).WillOnce(Return(149));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(49), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(350));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(150), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(751));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(351), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(2999));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(599), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(2999));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(999), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();
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
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
}

TEST_F(RouterRetryStateImplTest, ZeroMaxRetriesHeader) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"},
                                                 {"x-envoy-retry-grpc-on", "cancelled"},
                                                 {"x-envoy-max-retries", "0"}};
  setup(request_headers);
  EXPECT_FALSE(request_headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-retry-grpc-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-max-retries"));
  EXPECT_TRUE(state_->enabled());

  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
            state_->shouldRetryReset(connect_failure_, callback_));

  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(0UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(0UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

// Check that if there are 0 remaining retries available but we get
// non-retriable headers, we return No rather than NoRetryLimitExceeded.
TEST_F(RouterRetryStateImplTest, NoPreferredOverLimitExceeded) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"},
                                                 {"x-envoy-max-retries", "1"}};
  setup(request_headers);

  Http::TestResponseHeaderMapImpl bad_response_headers{{":status", "503"}};
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(bad_response_headers, callback_));

  Http::TestResponseHeaderMapImpl good_response_headers{{":status", "200"}};
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(good_response_headers, callback_));

  EXPECT_EQ(0UL, cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(0UL, virtual_cluster_.stats().upstream_rq_retry_limit_exceeded_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, virtual_cluster_.stats().upstream_rq_retry_.value());
}

TEST_F(RouterRetryStateImplTest, BudgetAvailableRetries) {
  // Expect no available retries from resource manager and override the max_retries CB via retry
  // budget. As configured, there are no allowed retries via max_retries CB.
  cluster_.resetResourceManagerWithRetryBudget(
      0 /* cx */, 0 /* rq_pending */, 0 /* rq */, 0 /* rq_retry */, 0 /* conn_pool */,
      20.0 /* budget_percent */, 3 /* min_retry_concurrency */);

  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};

  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, BudgetNoAvailableRetries) {
  // Expect no available retries from resource manager. Override the max_retries CB via a retry
  // budget that won't let any retries. As configured, there are 5 allowed retries via max_retries
  // CB.
  cluster_.resetResourceManagerWithRetryBudget(
      0 /* cx */, 0 /* rq_pending */, 20 /* rq */, 5 /* rq_retry */, 0 /* conn_pool */,
      0 /* budget_percent */, 0 /* min_retry_concurrency */);

  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};

  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryHeaders(response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, BudgetVerifyMinimumConcurrency) {
  // Expect no available retries from resource manager.
  cluster_.resetResourceManagerWithRetryBudget(
      0 /* cx */, 0 /* rq_pending */, 0 /* rq */, 0 /* rq_retry */, 0 /* conn_pool */,
      20.0 /* budget_percent */, 3 /* min_retry_concurrency */);

  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"},
                                                 {"x-envoy-max-retries", "42"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};

  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  // Load up 2 outstanding retries and verify the 3rd one is allowed when there are no outstanding
  // requests. This verifies the minimum allowed outstanding retries before the budget is scaled
  // with the request concurrency.
  incrOutstandingResource(TestResourceType::Retry, 2);

  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));

  // 3 outstanding retries.
  incrOutstandingResource(TestResourceType::Retry, 1);

  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryHeaders(response_headers, callback_));

  incrOutstandingResource(TestResourceType::Request, 20);

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));

  // 4 outstanding retries.
  incrOutstandingResource(TestResourceType::Retry, 1);

  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryHeaders(response_headers, callback_));

  // Override via runtime and expect successful retry.
  std::string value("100");
  EXPECT_CALL(cluster_.runtime_.snapshot_, get("fake_clusterretry_budget.budget_percent"))
      .WillRepeatedly(Return(value));
  EXPECT_CALL(cluster_.runtime_.snapshot_, getDouble("fake_clusterretry_budget.budget_percent", _))
      .WillRepeatedly(Return(100.0));

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, BudgetRuntimeSetOnly) {
  // Expect no available retries from resource manager, so no retries allowed according to
  // max_retries CB. Don't configure retry budgets. We'll rely on runtime config only.
  cluster_.resetResourceManager(0 /* cx */, 0 /* rq_pending */, 0 /* rq */, 0 /* rq_retry */,
                                0 /* conn_pool */);

  std::string value("20");
  EXPECT_CALL(cluster_.runtime_.snapshot_, get("fake_clusterretry_budget.min_retry_concurrency"))
      .WillRepeatedly(Return(value));
  EXPECT_CALL(cluster_.runtime_.snapshot_, get("fake_clusterretry_budget.budget_percent"))
      .WillRepeatedly(Return(value));
  EXPECT_CALL(cluster_.runtime_.snapshot_, getDouble("fake_clusterretry_budget.budget_percent", _))
      .WillRepeatedly(Return(20.0));

  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};

  setup(request_headers);
  EXPECT_TRUE(state_->enabled());

  incrOutstandingResource(TestResourceType::Retry, 2);

  expectTimerCreateAndEnable();
  Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, ParseRetryOn) {
  // RETRY_ON_5XX             0x1
  // RETRY_ON_GATEWAY_ERROR   0x2
  // RETRY_ON_CONNECT_FAILURE 0x4
  std::string config = "5xx,gateway-error,connect-failure";
  auto result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 7);
  EXPECT_TRUE(result.second);

  config = "xxx,gateway-error,connect-failure";
  result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 6);
  EXPECT_FALSE(result.second);

  config = " 5xx,gateway-error ,  connect-failure   ";
  result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 7);
  EXPECT_TRUE(result.second);

  config = " 5 xx,gateway-error ,  connect-failure   ";
  result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 6);
  EXPECT_FALSE(result.second);
}

TEST_F(RouterRetryStateImplTest, ParseRetryGrpcOn) {
  // RETRY_ON_GRPC_CANCELLED             0x20
  // RETRY_ON_GRPC_DEADLINE_EXCEEDED     0x40
  // RETRY_ON_GRPC_RESOURCE_EXHAUSTED    0x80
  std::string config = "cancelled,deadline-exceeded,resource-exhausted";
  auto result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 224);
  EXPECT_TRUE(result.second);

  config = "cancelled,deadline-exceeded,resource-exhaust";
  result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 96);
  EXPECT_FALSE(result.second);

  config = "   cancelled,deadline-exceeded   ,   resource-exhausted   ";
  result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 224);
  EXPECT_TRUE(result.second);

  config = "   cancelled,deadline-exceeded   ,   resource- exhausted   ";
  result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 96);
  EXPECT_FALSE(result.second);
}

} // namespace
} // namespace Router
} // namespace Envoy
