#include "envoy/registry/registry.h"

#include "extensions/retry_policy/conditional_retry/config.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace RetryPolicy {
namespace ConditionalRetry {

TEST(ConfigFactoryTest, TestFactory) {
  auto factory = Registry::FactoryRegistry<Router::RetryPolicyFactory>::getFactory(
      RetryPolicyValues::get().ConditionalRetry);
  EXPECT_EQ(factory->name(), RetryPolicyValues::get().ConditionalRetry);

  const std::string yaml = R"EOF(
      retry_conditions:
      - request_header:
          name: ":method"
          exact_match: "GET"
        retry_on: "connect-failure"
)EOF";
  ProtobufTypes::MessagePtr proto_config = factory->createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  auto policy =
      factory->createRetryPolicy(*proto_config, Http::TestRequestHeaderMapImpl{{":method", "GET"}},
                                 Envoy::ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(nullptr, policy);
}

TEST(ConfigFactoryTest, TestConditionalRetry) {
  auto factory = Registry::FactoryRegistry<Router::RetryPolicyFactory>::getFactory(
      RetryPolicyValues::get().ConditionalRetry);
  EXPECT_EQ(factory->name(), RetryPolicyValues::get().ConditionalRetry);

  const std::string yaml = R"EOF(
      retry_conditions:
      - request_header:
          name: ":method"
          exact_match: "GET"
        retry_on: "5xx,gateway-error,connect-failure,refused-stream,retriable-4xx,retriable-status-codes,reset,cancelled,deadline-exceeded,resource-exhausted,unavailable,internal"
        retriable_status_codes: [400]
      - request_header:
          name: ":method"
          exact_match: "POST"
        retry_on: "connect-failure"
)EOF";
  ProtobufTypes::MessagePtr proto_config = factory->createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  {
    auto policy = factory->createRetryPolicy(*proto_config,
                                             Http::TestRequestHeaderMapImpl{{":method", "GET"}},
                                             Envoy::ProtobufMessage::getStrictValidationVisitor());
    EXPECT_NE(nullptr, policy);
    EXPECT_FALSE(policy->shouldRetry());
    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}});
    EXPECT_TRUE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "502"}});
    EXPECT_TRUE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "409"}});
    EXPECT_TRUE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "400"}});
    EXPECT_TRUE(policy->shouldRetry());

    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "0"}});
    EXPECT_FALSE(policy->shouldRetry());

    // Canceled
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}});
    EXPECT_TRUE(policy->shouldRetry());

    // DeadlineExceeded
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "4"}});
    EXPECT_TRUE(policy->shouldRetry());

    // ResourceExhausted
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "8"}});
    EXPECT_TRUE(policy->shouldRetry());

    // Unavailable
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "14"}});
    EXPECT_TRUE(policy->shouldRetry());

    // Internal
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "13"}});
    EXPECT_TRUE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::Overflow);
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::RemoteReset);
    EXPECT_TRUE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::RemoteRefusedStreamReset);
    EXPECT_TRUE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::ConnectionFailure);
    EXPECT_TRUE(policy->shouldRetry());
  }
  {
    auto policy = factory->createRetryPolicy(*proto_config,
                                             Http::TestRequestHeaderMapImpl{{":method", "POST"}},
                                             Envoy::ProtobufMessage::getStrictValidationVisitor());
    EXPECT_NE(nullptr, policy);
    EXPECT_FALSE(policy->shouldRetry());
    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "502"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "409"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "400"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "0"}});
    EXPECT_FALSE(policy->shouldRetry());

    // Canceled
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}});
    EXPECT_FALSE(policy->shouldRetry());

    // DeadlineExceeded
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "4"}});
    EXPECT_FALSE(policy->shouldRetry());

    // ResourceExhausted
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "8"}});
    EXPECT_FALSE(policy->shouldRetry());

    // Unavailable
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "14"}});
    EXPECT_FALSE(policy->shouldRetry());

    // Internal
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "13"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::Overflow);
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::RemoteReset);
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::RemoteRefusedStreamReset);
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::ConnectionFailure);
    EXPECT_TRUE(policy->shouldRetry());
  }
  {

    auto policy = factory->createRetryPolicy(*proto_config,
                                             Http::TestRequestHeaderMapImpl{{":method", "PUT"}},
                                             Envoy::ProtobufMessage::getStrictValidationVisitor());
    EXPECT_NE(nullptr, policy);
    EXPECT_FALSE(policy->shouldRetry());
    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "502"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "409"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(Http::TestResponseHeaderMapImpl{{":status", "400"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "0"}});
    EXPECT_FALSE(policy->shouldRetry());

    // Canceled
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}});
    EXPECT_FALSE(policy->shouldRetry());

    // DeadlineExceeded
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "4"}});
    EXPECT_FALSE(policy->shouldRetry());

    // ResourceExhausted
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "8"}});
    EXPECT_FALSE(policy->shouldRetry());

    // Unavailable
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "14"}});
    EXPECT_FALSE(policy->shouldRetry());

    // Internal
    policy->recordResponseHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "13"}});
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::Overflow);
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::RemoteReset);
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::RemoteRefusedStreamReset);
    EXPECT_FALSE(policy->shouldRetry());

    policy->recordReset(Http::StreamResetReason::ConnectionFailure);
    EXPECT_FALSE(policy->shouldRetry());
  }
}
} // namespace ConditionalRetry
} // namespace RetryPolicy
} // namespace Extensions
} // namespace Envoy