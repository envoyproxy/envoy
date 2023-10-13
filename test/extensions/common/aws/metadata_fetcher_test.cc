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
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

MATCHER_P(WithName, expectedName, "") { return arg.name() == expectedName; }

MATCHER_P(WithAttribute, expectedCluster, "") {
  const auto argSocketAddress =
      arg.load_assignment().endpoints()[0].lb_endpoints()[0].endpoint().address().socket_address();
  const auto expectedSocketAddress = expectedCluster.load_assignment()
                                         .endpoints()[0]
                                         .lb_endpoints()[0]
                                         .endpoint()
                                         .address()
                                         .socket_address();
  return arg.name() == expectedCluster.name() &&
         argSocketAddress.address() == expectedSocketAddress.address() &&
         argSocketAddress.port_value() == expectedSocketAddress.port_value();
}

class MetadataFetcherTest : public testing::Test {
public:
  void setupFetcher() {
    mock_factory_ctx_.cluster_manager_.initializeThreadLocalClusters({"cluster_name"});
    fetcher_ = MetadataFetcher::create(mock_factory_ctx_.cluster_manager_, "cluster_name");
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

  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "200", "not_empty");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(testing::_));
  EXPECT_CALL(receiver, onMetadataError(testing::_)).Times(0);

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestRequestMatch) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost("169.254.170.2:80");
  message.headers().setPath("/v2/credentials/c68caeb5-ef71-4914-8170-111111111111");
  message.headers().setCopy(Http::LowerCaseString(":pseudo-header"), "peudo-header-value");
  message.headers().setCopy(Http::LowerCaseString("X-aws-ec2-metadata-token"), "Token");

  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "200", "not_empty");
  MockMetadataReceiver receiver;

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.thread_local_cluster_.async_client_,
              send_(_, _, _))
      .WillOnce(Invoke([](Http::RequestMessagePtr& request, Http::AsyncClient::Callbacks&,
                          const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        EXPECT_EQ("169.254.170.2", request->headers().getHostValue());
        EXPECT_EQ("/v2/credentials/c68caeb5-ef71-4914-8170-111111111111",
                  request->headers().getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, request->headers().getMethodValue());
        EXPECT_EQ(Http::Headers::get().SchemeValues.Http, request->headers().getSchemeValue());
        EXPECT_EQ("Token", request->headers()
                               .get(Http::LowerCaseString("X-aws-ec2-metadata-token"))[0]
                               ->value()
                               .getStringView());
        EXPECT_TRUE(request->headers().get(Http::LowerCaseString(":pseudo-header")).empty());
        return nullptr;
      }));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestGet400) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "400", "not_empty");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(testing::_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestGet400NoBody) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "400", "");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(testing::_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestGetNoBody) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "200", "");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(testing::_)).Times(0);
  EXPECT_CALL(receiver,
              onMetadataError(MetadataFetcher::MetadataReceiver::Failure::InvalidMetadata));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestHttpFailure) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;

  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_,
                           Http::AsyncClient::FailureReason::Reset);
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataSuccess(testing::_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestWithValidCluster) {
  // Setup with thread local cluster
  Http::RequestMessageImpl message;
  message.headers().setScheme(Http::Headers::get().SchemeValues.Http);
  message.headers().setMethod(Http::Headers::get().MethodValues.Get);
  message.headers().setHost("169.254.170.2:80");
  message.headers().setPath("/v2/credentials/c68caeb5-ef71-4914-8170-111111111111");

  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  fetcher_ = MetadataFetcher::create(mock_factory_ctx_.cluster_manager_, "cluster_name");
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(&cluster_));

  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "200", "not_empty");
  MockMetadataReceiver receiver;
  EXPECT_CALL(receiver, onMetadataError(testing::_)).Times(0);

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestClusterNotFound) {
  // Setup without thread local cluster
  fetcher_ = MetadataFetcher::create(mock_factory_ctx_.cluster_manager_, "cluster_name");
  Http::RequestMessageImpl message;
  MockMetadataReceiver receiver;

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(receiver, onMetadataSuccess(testing::_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(MetadataFetcher::MetadataReceiver::Failure::MissingConfig));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestCancel) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;
  Http::MockAsyncClientRequest request(
      &(mock_factory_ctx_.cluster_manager_.thread_local_cluster_.async_client_));
  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, &request);
  MockMetadataReceiver receiver;
  EXPECT_CALL(request, cancel());
  EXPECT_CALL(receiver, onMetadataSuccess(testing::_)).Times(0);
  EXPECT_CALL(receiver, onMetadataError(testing::_)).Times(0);

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
  // Proper cancel
  fetcher_->cancel();
  // Re-entrant cancel
  fetcher_->cancel();
}

TEST_F(MetadataFetcherTest, TestSpanPassedDown) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;
  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "200", "not_empty");
  MockMetadataReceiver receiver;

  // Expectations for span
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.thread_local_cluster_.async_client_,
              send_(_, _, _))
      .WillOnce(Invoke(
          [this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
                 const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_TRUE(options.parent_span_ == &this->parent_span_);
            EXPECT_TRUE(options.child_span_name_ == "AWS Metadata Fetch");
            return nullptr;
          }));

  // Act
  fetcher_->fetch(message, parent_span_, receiver);
}

TEST_F(MetadataFetcherTest, TestDefaultRetryPolicy) {
  // Setup
  setupFetcher();
  Http::RequestMessageImpl message;
  MockUpstream mock_result(mock_factory_ctx_.cluster_manager_, "200", "not_empty");
  MockMetadataReceiver receiver;

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.thread_local_cluster_.async_client_,
              send_(_, _, _))
      .WillOnce(Invoke(
          [](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
             const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_TRUE(options.retry_policy.has_value());
            EXPECT_TRUE(options.buffer_body_for_retry);
            EXPECT_TRUE(options.retry_policy.value().has_num_retries());
            EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(options.retry_policy.value(), num_retries), 3);

            EXPECT_TRUE(options.retry_policy.value().has_per_try_timeout());
            EXPECT_EQ(PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value(), per_try_timeout),
                      5000);

            EXPECT_TRUE(options.retry_policy.value().has_per_try_idle_timeout());
            EXPECT_EQ(PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value(), per_try_idle_timeout),
                      1000);

            const std::string& retry_on = options.retry_policy.value().retry_on();
            std::set<std::string> retry_on_modes = absl::StrSplit(retry_on, ',');

            EXPECT_EQ(retry_on_modes.count("5xx"), 1);
            EXPECT_EQ(retry_on_modes.count("gateway-error"), 1);
            EXPECT_EQ(retry_on_modes.count("connect-failure"), 1);
            EXPECT_EQ(retry_on_modes.count("reset"), 1);

            return nullptr;
          }));

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
