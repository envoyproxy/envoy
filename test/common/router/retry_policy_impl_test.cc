#include "envoy/config/route/v3/route_components.pb.validate.h"

#include "common/http/header_map_impl.h"
#include "common/router/retry_policy_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

envoy::config::route::v3::RetryPolicy
parseRetryPolicyConfigurationFromYaml(const std::string& yaml) {
  envoy::config::route::v3::RetryPolicy policy;
  TestUtility::loadFromYaml(yaml, policy);
  TestUtility::validate(policy);
  return policy;
}

class RetryPolicyImplTest : public testing::Test {
public:
  void setup(const envoy::config::route::v3::RetryPolicy& retry_policy) {
    policy_ = RetryPolicyImpl(retry_policy, ProtobufMessage::getStrictValidationVisitor());
  }

  const Http::StreamResetReason remote_reset_{Http::StreamResetReason::RemoteReset};
  const Http::StreamResetReason remote_refused_stream_reset_{
      Http::StreamResetReason::RemoteRefusedStreamReset};
  const Http::StreamResetReason overflow_reset_{Http::StreamResetReason::Overflow};
  const Http::StreamResetReason connect_failure_{Http::StreamResetReason::ConnectionFailure};

  RetryPolicyImpl policy_;
};

TEST_F(RetryPolicyImplTest, DefaultRetryPolicy) {
  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};

  EXPECT_EQ(policy_.retryOn(), 0);
  EXPECT_TRUE(policy_.retriableHeaders().empty());
  EXPECT_TRUE(policy_.retriableRequestHeaders().empty());
  EXPECT_FALSE(policy_.enabled());
  EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  EXPECT_FALSE(policy_.wouldRetryFromReset(connect_failure_));
  EXPECT_EQ(policy_.perTryTimeout(), std::chrono::milliseconds(0));
  EXPECT_TRUE(policy_.retryHostPredicates().empty());
  EXPECT_EQ(policy_.retryPriority(), nullptr);
  EXPECT_EQ(policy_.hostSelectionMaxAttempts(), 1);
  EXPECT_EQ(policy_.numRetries(), 1);
  EXPECT_EQ(policy_.remainingRetries(), 1);
  EXPECT_EQ(policy_.baseInterval(), absl::nullopt);
  EXPECT_EQ(policy_.maxInterval(), absl::nullopt);
}

TEST_F(RetryPolicyImplTest, PolicyRefusedStream) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "refused-stream"}};
  policy_.recordRequestHeader(request_headers);

  EXPECT_NE(policy_.retryOn() & CoreRetryPolicy::RETRY_ON_REFUSED_STREAM, 0);
  EXPECT_TRUE(policy_.enabled());
  EXPECT_TRUE(policy_.wouldRetryFromReset(remote_refused_stream_reset_));
}

TEST_F(RetryPolicyImplTest, Policy5xxResetOverflow) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  policy_.recordRequestHeader(request_headers);

  EXPECT_TRUE(policy_.enabled());
  EXPECT_FALSE(policy_.wouldRetryFromReset(overflow_reset_));
}

TEST_F(RetryPolicyImplTest, Policy5xxRemoteReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  policy_.recordRequestHeader(request_headers);

  EXPECT_TRUE(policy_.enabled());
  EXPECT_TRUE(policy_.wouldRetryFromReset(remote_reset_));
}

TEST_F(RetryPolicyImplTest, Policy5xxRemote503) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, Policy5xxRemote503Overloaded) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"},
                                                   {"x-envoy-overloaded", "true"}};
  EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyResourceExhaustedRemoteRateLimited) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "resource-exhausted"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"grpc-status", "8"}, {"x-envoy-ratelimited", "true"}};
  EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGatewayErrorRemote502) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "502"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGatewayErrorRemote503) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGatewayErrorRemote504) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "504"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGatewayErrorResetOverflow) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  EXPECT_FALSE(policy_.wouldRetryFromReset(overflow_reset_));
}

TEST_F(RetryPolicyImplTest, PolicyGatewayErrorRemoteReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "gateway-error"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  EXPECT_TRUE(policy_.wouldRetryFromReset(remote_reset_));
}

TEST_F(RetryPolicyImplTest, PolicyGrpcCancelled) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "cancelled"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "1"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGrpcDeadlineExceeded) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "deadline-exceeded"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "4"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGrpcResourceExhausted) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "resource-exhausted"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "8"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGrpcUnavilable) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "unavailable"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "14"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyGrpcInternal) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-grpc-on", "internal"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"grpc-status", "13"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyConnectFailureOtherReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  EXPECT_FALSE(policy_.wouldRetryFromReset(remote_reset_));
}

TEST_F(RetryPolicyImplTest, PolicyConnectFailureResetConnectFailure) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  EXPECT_TRUE(policy_.wouldRetryFromReset(connect_failure_));
}

TEST_F(RetryPolicyImplTest, PolicyRetriable4xxRetry) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "409"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyRetriable4xxNoRetry) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "400"}};
  EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, PolicyRetriable4xxReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-4xx"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  EXPECT_FALSE(policy_.wouldRetryFromReset(remote_reset_));
}

TEST_F(RetryPolicyImplTest, RetriableStatusCodes) {
  const std::string yaml = R"EOF(
    retriable_status_codes: [409]
  )EOF";
  setup(parseRetryPolicyConfigurationFromYaml(yaml));
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "409"}};
  EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
}

TEST_F(RetryPolicyImplTest, RetriableStatusCodesUpstreamReset) {
  const std::string yaml = R"EOF(
    retriable_status_codes: [409]
  )EOF";
  setup(parseRetryPolicyConfigurationFromYaml(yaml));
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  EXPECT_FALSE(policy_.wouldRetryFromReset(remote_reset_));
}

TEST_F(RetryPolicyImplTest, RetriableStatusCodesHeader) {
  auto setup = [this](Http::TestRequestHeaderMapImpl request_headers) {
    policy_ = RetryPolicyImpl();
    policy_.recordRequestHeader(request_headers);
    EXPECT_TRUE(policy_.enabled());
  };

  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"},
                                                   {"x-envoy-retriable-status-codes", "200"}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-status-codes"},
                                                   {"x-envoy-retriable-status-codes", "418,200"}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-status-codes"},
        {"x-envoy-retriable-status-codes", "   418 junk,200"}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-status-codes"},
        {"x-envoy-retriable-status-codes", "   418 junk,xxx200"}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }
}

// Test that when 'retriable-headers' policy is set via request header, certain configured headers
// trigger retries.
TEST_F(RetryPolicyImplTest, RetriableHeadersPolicySetViaRequestHeader) {
  const std::string yaml = R"EOF(
    retry_on: "5xx"
    retriable_headers: 
    - name: "X-Upstream-Pushback"
  )EOF";

  auto config = parseRetryPolicyConfigurationFromYaml(yaml);
  auto setup = [this, config](Http::TestRequestHeaderMapImpl request_headers) {
    this->setup(config);
    policy_.recordRequestHeader(request_headers);
    EXPECT_TRUE(policy_.enabled());
  };

  // No retries based on response headers: retry mode isn't enabled.
  {
    Http::TestRequestHeaderMapImpl request_headers;
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "true"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }

  // Retries based on response headers: retry mode enabled via request header.
  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "retriable-headers"}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "true"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
}

// Test that when 'retriable-headers' policy is set via retry policy configuration,
// configured header matcher conditions trigger retries.
TEST_F(RetryPolicyImplTest, RetriableHeadersPolicyViaRetryPolicyConfiguration) {
  const std::string yaml = R"EOF(
    retry_on: "retriable-headers"
    retriable_headers: 
    - name: "X-Upstream-Pushback"
    - name: "should-retry"
      exact_match: "yes"
    - name: "X-Verdict"
      prefix_match: "retry"
    - name: ":status"
      range_match: 
        start: 500
        end: 505
  )EOF";
  setup(parseRetryPolicyConfigurationFromYaml(yaml));

  // matcher1: header presence (any value).
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "true"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-upstream-pushback", "false"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }

  // matcher2: exact header value match.
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"should-retry", "yes"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"should-retry", "no"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }

  // matcher3: prefix match.
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-verdict", "retry-please"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-verdict", "dont-retry-please"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }

  // matcher4: status code range (note half-open semantics: [start, end)).
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "499"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "504"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "505"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }
}

// Test various combinations of retry headers set via request headers.
TEST_F(RetryPolicyImplTest, RetriableHeadersSetViaRequestHeader) {
  auto setup = [this](Http::TestRequestHeaderMapImpl request_headers) {
    policy_ = RetryPolicyImpl();
    policy_.recordRequestHeader(request_headers);
    EXPECT_TRUE(policy_.enabled());
  };

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-headers"},
        {"x-envoy-retriable-header-names", "X-Upstream-Pushback,FOOBAR"}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{"x-upstream-pushback", "yes"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-headers"},
        {"x-envoy-retriable-header-names", "X-Upstream-Pushback,  FOOBAR  "}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{"foobar", "false"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retry-on", "retriable-headers"},
        {"x-envoy-retriable-header-names", "X-Upstream-Pushback,,FOOBAR"}};
    setup(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }
}

// Test merging retriable headers set via request headers and via config file.
TEST_F(RetryPolicyImplTest, RetriableHeadersMergedConfigAndRequestHeaders) {
  // Config says: retry if response is not 200.
  const std::string yaml = R"EOF(
    retry_on: "retriable-headers"
    retriable_headers:
    - name: ":status"
      exact_match: "200"
      invert_match: true
  )EOF";
  setup(parseRetryPolicyConfigurationFromYaml(yaml));

  // No retries according to config.
  {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }

  // Request header supplements the config: as a result we retry on 200.
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"x-envoy-retriable-header-names", "  :status,  FOOBAR  "}};
    policy_.recordRequestHeader(request_headers);
    EXPECT_TRUE(policy_.enabled());

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }
}

TEST_F(RetryPolicyImplTest, PolicyResetRemoteReset) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "reset"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_TRUE(policy_.enabled());

  EXPECT_TRUE(policy_.wouldRetryFromReset(remote_reset_));
}

TEST_F(RetryPolicyImplTest, PolicyLimitedByRequestHeaders) {
  const std::string yaml = R"EOF(
    retry_on: "retriable-headers"
    retriable_request_headers:
    - name: ":method"
      exact_match: "GET"
    - name: ":method"
      exact_match: "HEAD"
  )EOF";
  auto config = parseRetryPolicyConfigurationFromYaml(yaml);

  {
    Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "5xx"}};
    setup(config);
    policy_.recordRequestHeader(request_headers);
    EXPECT_FALSE(policy_.enabled());
  }

  {

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {"x-envoy-retry-on", "retriable-4xx"}};
    setup(config);
    policy_.recordRequestHeader(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "409"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {"x-envoy-retry-on", "5xx"}};
    setup(config);
    policy_.recordRequestHeader(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "HEAD"},
                                                   {"x-envoy-retry-on", "5xx"}};
    setup(config);
    policy_.recordRequestHeader(request_headers);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    EXPECT_TRUE(policy_.wouldRetryFromHeaders(response_headers));
  }

  // Sanity check that we're only enabling retries for the configured retry-on.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "HEAD"},
                                                   {"x-envoy-retry-on", "retriable-4xx"}};
    setup(config);
    policy_.recordRequestHeader(request_headers);
    Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
    EXPECT_FALSE(policy_.wouldRetryFromHeaders(response_headers));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                   {"x-envoy-retry-on", "5xx"}};
    setup(config);
    policy_.recordRequestHeader(request_headers);
    EXPECT_FALSE(policy_.enabled());
  }
}

TEST_F(RetryPolicyImplTest, RouteConfigNoRetriesAllowed) {
  const std::string yaml = R"EOF(
    num_retries: 0
    retry_on: "connect-failure"
  )EOF";
  setup(parseRetryPolicyConfigurationFromYaml(yaml));

  EXPECT_TRUE(policy_.enabled());
  EXPECT_EQ(policy_.numRetries(), 0);
  EXPECT_EQ(policy_.remainingRetries(), 0);
}

TEST_F(RetryPolicyImplTest, RouteConfigNoHeaderConfig) {
  const std::string yaml = R"EOF(
    num_retries: 1
    retry_on: "connect-failure"
  )EOF";
  setup(parseRetryPolicyConfigurationFromYaml(yaml));
  EXPECT_TRUE(policy_.enabled());

  EXPECT_TRUE(policy_.wouldRetryFromReset(connect_failure_));
}

TEST_F(RetryPolicyImplTest, MaxRetriesHeader) {
  // The max retries header will take precedence over the policy
  const std::string yaml = R"EOF(
    num_retries: 4
  )EOF";
  setup(parseRetryPolicyConfigurationFromYaml(yaml));

  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"},
                                                 {"x-envoy-retry-grpc-on", "cancelled"},
                                                 {"x-envoy-max-retries", "3"}};
  policy_.recordRequestHeader(request_headers);

  EXPECT_FALSE(request_headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-retry-grpc-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-max-retries"));
  EXPECT_TRUE(policy_.enabled());

  EXPECT_TRUE(policy_.wouldRetryFromReset(connect_failure_));
  EXPECT_EQ(policy_.numRetries(), 3);
  EXPECT_EQ(policy_.remainingRetries(), 3);
}

TEST_F(RetryPolicyImplTest, ZeroMaxRetriesHeader) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "connect-failure"},
                                                 {"x-envoy-retry-grpc-on", "cancelled"},
                                                 {"x-envoy-max-retries", "0"}};
  policy_.recordRequestHeader(request_headers);
  EXPECT_FALSE(request_headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-retry-grpc-on"));
  EXPECT_FALSE(request_headers.has("x-envoy-max-retries"));
  EXPECT_TRUE(policy_.enabled());

  EXPECT_EQ(policy_.numRetries(), 0);
  EXPECT_EQ(policy_.remainingRetries(), 0);
}

} // namespace
} // namespace Router
} // namespace Envoy