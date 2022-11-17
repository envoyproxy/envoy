#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_impl.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/gcp_authn/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;
using Server::Configuration::MockFactoryContext;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using Upstream::MockThreadLocalCluster;

constexpr char DefaultConfig[] = R"EOF(
    http_uri:
      uri: http://testhost/path/test
      cluster: test_cluster
      timeout:
        seconds: 5
    retry_policy:
      retry_back_off:
        base_interval: 1s
        max_interval: 10s
      num_retries: 5
  )EOF";

class GcpAuthnFilterTest : public testing::Test {
public:
  GcpAuthnFilterTest() {
    // Initialize the default configuration.
    TestUtility::loadFromYaml(DefaultConfig, config_);
    filter_config_ =
        std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
            config_);
  }

  void setupMockObjects() {
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(_))
        .WillRepeatedly(Return(&thread_local_cluster_));
    EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _))
        .WillRepeatedly(Invoke([&](Envoy::Http::RequestMessagePtr& message,
                                   Envoy::Http::AsyncClient::Callbacks& callback,
                                   const Envoy::Http::AsyncClient::RequestOptions& options)
                                   -> Http::AsyncClient::Request* {
          message_.swap(message);
          client_callback_ = &callback;
          options_ = options;
          return &client_request_;
        }));
  }

  void setupFilterAndCallback() {
    filter_ = std::make_unique<GcpAuthnFilter>(filter_config_, context_, "stats", nullptr);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void setupMockFilterMetadata(bool valid) {
    // Set up mock filter metadata.
    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_));
    if (valid) {
      envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
      audience.set_url("test");

      (*metadata_.mutable_typed_filter_metadata())
          [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
              .PackFrom(audience);
    }
    ON_CALL(*cluster_info_, metadata()).WillByDefault(testing::ReturnRef(metadata_));
  }

  void overrideConfig(const GcpAuthnFilterConfig& config) {
    config_ = config;
    filter_config_ =
        std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
            config);
  }

  void createClient() { client_ = std::make_unique<GcpAuthnClient>(config_, context_); }

  NiceMock<MockFactoryContext> context_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Envoy::Http::MockAsyncClientRequest> client_request_{
      &thread_local_cluster_.async_client_};
  MockRequestCallbacks request_callbacks_;

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* client_callback_;
  Envoy::Http::RequestMessagePtr message_;
  Envoy::Http::AsyncClient::RequestOptions options_;

  std::unique_ptr<GcpAuthnClient> client_;
  std::unique_ptr<GcpAuthnFilter> filter_;
  GcpAuthnFilterConfig config_;
  FilterConfigSharedPtr filter_config_;
  Http::TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  envoy::config::core::v3::Metadata metadata_;
};

TEST_F(GcpAuthnFilterTest, Success) {
  setupMockObjects();
  // Create the client object.
  createClient();

  client_->fetchToken(request_callbacks_, buildRequest(config_.http_uri().uri()));
  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  EXPECT_EQ(message_->headers().Host()->value().getStringView(), "testhost");
  EXPECT_EQ(message_->headers().Path()->value().getStringView(), "/path/test");

  EXPECT_EQ(options_.retry_policy->num_retries().value(), 5);
  EXPECT_EQ(options_.retry_policy->retry_back_off().base_interval().seconds(), 1);
  EXPECT_EQ(options_.retry_policy->retry_back_off().max_interval().seconds(), 10);
  EXPECT_EQ(options_.retry_policy->retry_on(), "5xx,gateway-error,connect-failure,reset");

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));

  EXPECT_CALL(request_callbacks_, onComplete(response.get()));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnFilterTest, NoCluster) {
  std::string no_cluster_config = R"EOF(
    http_uri:
      uri: http://testhost/path/test
      timeout:
        seconds: 5
    retry_policy:
      retry_back_off:
        base_interval: 1s
        max_interval: 10s
      num_retries: 5
  )EOF";

  // The pointer of thread local cluster is expected to be nullptr and http async client is not
  // expected to be called since `cluster` is not configured.
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_, httpAsyncClient()).Times(0);

  EXPECT_CALL(request_callbacks_, onComplete(/*response_ptr=*/nullptr));
  GcpAuthnFilterConfig config;
  TestUtility::loadFromYaml(no_cluster_config, config);
  overrideConfig(config);
  createClient();
  client_->fetchToken(request_callbacks_, buildRequest(config.http_uri().uri()));
}

TEST_F(GcpAuthnFilterTest, Failure) {
  setupMockObjects();
  // Create the client object.
  createClient();
  EXPECT_CALL(request_callbacks_, onComplete(/*response_ptr=*/nullptr));
  client_->fetchToken(request_callbacks_, buildRequest(config_.http_uri().uri()));
  client_callback_->onFailure(client_request_, Http::AsyncClient::FailureReason::Reset);
}

TEST_F(GcpAuthnFilterTest, NotOkResponse) {
  setupMockObjects();
  // Create the client object.
  createClient();

  client_->fetchToken(request_callbacks_, buildRequest(config_.http_uri().uri()));

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "504"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  EXPECT_CALL(request_callbacks_, onComplete(/*response_ptr=*/nullptr));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnFilterTest, EmptyResponseHeader) {
  setupMockObjects();
  // Create the client object.
  createClient();

  client_->fetchToken(request_callbacks_, buildRequest(config_.http_uri().uri()));

  Envoy::Http::ResponseHeaderMapPtr empty_resp_headers(
      new Envoy::Http::TestResponseHeaderMapImpl({}));
  Envoy::Http::ResponseMessagePtr empty_response(
      new Envoy::Http::ResponseMessageImpl(std::move(empty_resp_headers)));
  EXPECT_CALL(request_callbacks_, onComplete(/*response_ptr=*/nullptr));
  client_callback_->onSuccess(client_request_, std::move(empty_response));
}

TEST_F(GcpAuthnFilterTest, NoRoute) {
  setupFilterAndCallback();

  // route() call return nullptr
  EXPECT_CALL(decoder_callbacks_, route()).WillOnce(Return(nullptr));
  // decodeHeaders() is expected to return `Continue` because nothing can really be done without
  // route.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(GcpAuthnFilterTest, NoFilterMetadata) {
  setupMockObjects();
  setupFilterAndCallback();
  // Set up mock filter metadata.
  setupMockFilterMetadata(/*valid=*/false);
  // decodeHeaders() is expected to return `Continue` because no filter metadata is specified
  // in configuration.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->stats().retrieve_audience_failed_.value(), 1);
}

TEST_F(GcpAuthnFilterTest, ResumeFilterChainIteration) {
  setupMockObjects();
  setupFilterAndCallback();
  // Set up mock filter metadata.
  setupMockFilterMetadata(/*valid=*/true);

  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  // continueDecoding() is expected to be called to resume the filter chain iteration after
  // onSuccess().
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnFilterTest, DestoryFilter) {
  setupMockObjects();
  setupFilterAndCallback();
  // Set up mock filter metadata.
  setupMockFilterMetadata(/*valid=*/true);

  // decodeHeaders() is expected to return `StopAllIterationAndWatermark` and state is expected to
  // be in `Calling` state because none of complete functions(i.e., onSuccess, onFailure, onDestroy,
  // etc) has been called.
  EXPECT_EQ(filter_->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopAllIterationAndWatermark);
  EXPECT_EQ(filter_->state(), GcpAuthnFilter::State::Calling);
  filter_->onDestroy();
  // onDestroy() call is expected to update the state from `Calling` to `Complete`.
  EXPECT_EQ(filter_->state(), GcpAuthnFilter::State::Complete);
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
