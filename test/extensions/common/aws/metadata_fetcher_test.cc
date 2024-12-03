#include <chrono>
#include <cstddef>
#include <thread>

#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/extensions/filters/http/common/mock.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using Envoy::Extensions::HttpFilters::Common::MockUpstream;
using testing::_;
using testing::AllOf;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

MATCHER_P(OptionsHasBufferBodyForRetry, expectedValue, "") {
  *result_listener << "\nexpected { buffer_body_for_retry: \"" << expectedValue
                   << "\"} but got {buffer_body_for_retry: \"" << arg.buffer_body_for_retry
                   << "\"}\n";
  return ExplainMatchResult(expectedValue, arg.buffer_body_for_retry, result_listener);
}

MATCHER_P(NumRetries, expectedRetries, "") {
  *result_listener << "\nexpected { num_retries: \"" << expectedRetries
                   << "\"} but got {num_retries: \"" << arg.num_retries().value() << "\"}\n";
  return ExplainMatchResult(expectedRetries, arg.num_retries().value(), result_listener);
}

MATCHER_P(PerTryTimeout, expectedTimeout, "") {
  *result_listener << "\nexpected { per_try_timeout: \"" << expectedTimeout
                   << "\"} but got { per_try_timeout: \"" << arg.per_try_timeout().seconds()
                   << "\"}\n";
  return ExplainMatchResult(expectedTimeout, arg.per_try_timeout().seconds(), result_listener);
}

MATCHER_P(PerTryIdleTimeout, expectedIdleTimeout, "") {
  *result_listener << "\nexpected { per_try_idle_timeout: \"" << expectedIdleTimeout
                   << "\"} but got { per_try_idle_timeout: \""
                   << arg.per_try_idle_timeout().seconds() << "\"}\n";
  return ExplainMatchResult(expectedIdleTimeout, arg.per_try_idle_timeout().seconds(),
                            result_listener);
}

MATCHER_P(RetryOnModes, expectedModes, "") {
  const std::string& retry_on = arg.retry_on();
  std::set<std::string> retry_on_modes = absl::StrSplit(retry_on, ',');
  *result_listener << "\nexpected retry_on modes doesn't match "
                   << "received { retry_on modes: \"" << retry_on << "\"}\n";
  return ExplainMatchResult(expectedModes, retry_on_modes, result_listener);
}

MATCHER_P(OptionsHasRetryPolicy, policyMatcher, "") {
  if (!arg.retry_policy.has_value()) {
    *result_listener << "Expected options to have retry policy, but it was unset";
    return false;
  }
  return ExplainMatchResult(policyMatcher, arg.retry_policy.value(), result_listener);
}

class MetadataFetcherTest : public testing::Test {
public:
  void setupFetcher() {

    mock_factory_ctx_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"cluster_name"});
    fetcher_ = MetadataFetcher::create(mock_factory_ctx_.server_factory_context_.cluster_manager_,
                                       "cluster_name");
    EXPECT_TRUE(fetcher_ != nullptr);
  }

  void setupFetcherShutDown() {
    ON_CALL(mock_factory_ctx_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
        .WillByDefault(Return(nullptr));
    ON_CALL(mock_factory_ctx_.server_factory_context_.cluster_manager_, isShutdown())
        .WillByDefault(Return(true));

    mock_factory_ctx_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"cluster_name"});
    fetcher_ = MetadataFetcher::create(mock_factory_ctx_.server_factory_context_.cluster_manager_,
                                       "cluster_name");
    EXPECT_TRUE(fetcher_ != nullptr);
  }

  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  std::unique_ptr<MetadataFetcher> fetcher_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

TEST_F(MetadataFetcherTest, TestGetSuccess) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;
  std::string body = "not_empty";
  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200", body);
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(std::move(body)));
  EXPECT_CALL(receiver, onMetadataError(_)).Times(0);

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestClusterShutdown) {
  // Setup
  setupFetcherShutDown();
  Http::RequestMessageImpl message;
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(_)).Times(0);

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestRequestMatchAndSpanPassedDown) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost("169.254.170.2:80");
  message.headers().setPath("/v2/credentials/c68caeb5-ef71-4914-8170-111111111111");
  message.headers().setCopy(Http::LowerCaseString(":pseudo-header"), "peudo-header-value");
  message.headers().setCopy(Http::LowerCaseString("X-aws-ec2-metadata-token"), "Token");

  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200",
                           "not_empty");
  MockMetadataReceiver receiver;
  Http::MockAsyncClientRequest httpClientRequest(
      &mock_factory_ctx_.server_factory_context_.cluster_manager_.thread_local_cluster_
           .async_client_);

  EXPECT_CALL(mock_factory_ctx_.server_factory_context_.cluster_manager_.thread_local_cluster_
                  .async_client_,
              send_(_, _, _))
      .WillOnce(Invoke(
          [this, &httpClientRequest](
              Http::RequestMessagePtr& request, Http::AsyncClient::Callbacks& cb,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            Http::TestRequestHeaderMapImpl injected_headers = {
                {":method", "GET"},
                {":scheme", "http"},
                {":authority", "169.254.170.2"},
                {":path", "/v2/credentials/c68caeb5-ef71-4914-8170-111111111111"},
                {"X-aws-ec2-metadata-token", "Token"}};
            EXPECT_THAT(request->headers(), IsSupersetOfHeaders(injected_headers));
            EXPECT_TRUE(request->headers().get(Http::LowerCaseString(":pseudo-header")).empty());

            // Verify expectations for span
            EXPECT_TRUE(options.parent_span_ == &this->parent_span_);
            EXPECT_TRUE(options.child_span_name_ == "AWS Metadata Fetch");

            // Let's say this ends up with a failure then verify it is handled properly by calling
            // onMetadataError.
            cb.onFailure(httpClientRequest, Http::AsyncClient::FailureReason::Reset);
            return &httpClientRequest;
          }));
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network));
  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestGet400) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_, "400",
                           "not_empty");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestGet400NoBody) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_, "400", "");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestGetNoBody) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200", "");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(_)).Times(0);
  EXPECT_CALL(receiver,
              onMetadataError(MetadataFetcher::MetadataReceiver::Failure::InvalidMetadata));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestHttpFailure) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_,
                           Http::AsyncClient::FailureReason::Reset);
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestClusterNotFound) {
  // Setup without thread local cluster
  fetcher_ = MetadataFetcher::create(mock_factory_ctx_.server_factory_context_.cluster_manager_,
                                     "cluster_name");
  Http::RequestMessageImpl message;
  MockMetadataReceiver receiver;

  EXPECT_CALL(mock_factory_ctx_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(receiver, onMetadataSuccess(_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::MissingConfig));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestCancel) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;
  Http::MockAsyncClientRequest request(&(mock_factory_ctx_.server_factory_context_.cluster_manager_
                                             .thread_local_cluster_.async_client_));
  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_, &request);
  MockMetadataReceiver receiver;
  EXPECT_CALL(request, cancel());
  EXPECT_CALL(receiver, onMetadataSuccess(_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(_)).Times(0);

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
  // Proper cancel
  fetcher_->cancel();
  Mock::VerifyAndClearExpectations(&request);
  Mock::VerifyAndClearExpectations(&receiver);
  // Re-entrant cancel should do nothing.
  EXPECT_CALL(request, cancel()).Times(0);
  fetcher_->cancel();
}

TEST_F(MetadataFetcherTest, TestDefaultRetryPolicy) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;
  MockUpstream mock_result(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200",
                           "not_empty");
  MockMetadataReceiver receiver;

  EXPECT_CALL(
      mock_factory_ctx_.server_factory_context_.cluster_manager_.thread_local_cluster_
          .async_client_,
      send_(_, _,
            AllOf(OptionsHasBufferBodyForRetry(true),
                  OptionsHasRetryPolicy(AllOf(
                      NumRetries(3), PerTryTimeout(5), PerTryIdleTimeout(1),
                      RetryOnModes(UnorderedElementsAre("5xx", "gateway-error", "connect-failure",
                                                        "refused-stream", "reset")))))))
      .WillOnce(Return(nullptr));
  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestFailureToStringConversion) {
  // Setup
  setupFetcher();
  EXPECT_EQ(fetcher_->failureToString(MetadataFetcher::MetadataReceiver::Failure::Network),
            "Network");
  EXPECT_EQ(fetcher_->failureToString(MetadataFetcher::MetadataReceiver::Failure::InvalidMetadata),
            "InvalidMetadata");
  EXPECT_EQ(fetcher_->failureToString(MetadataFetcher::MetadataReceiver::Failure::MissingConfig),
            "MissingConfig");
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
