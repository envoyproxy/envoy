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

using envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;
using Server::Configuration::MockFactoryContext;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
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
          return nullptr;
        }));
  }

  void createClient(const std::string& config_str = DefaultConfig) {
    TestUtility::loadFromYaml(config_str, config_);
    client_ = std::make_unique<GcpAuthnClient>(config_, context_);
  }
  void createClient(const GcpAuthnFilterConfig& config) {
    client_ = std::make_unique<GcpAuthnClient>(config, context_);
  }

  NiceMock<MockFactoryContext> context_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Envoy::Http::MockAsyncClientRequest client_request_{&thread_local_cluster_.async_client_};
  MockRequestCallbacks request_callbacks_;

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* client_callback_;
  Envoy::Http::RequestMessagePtr message_;
  Envoy::Http::AsyncClient::RequestOptions options_;

  std::unique_ptr<GcpAuthnClient> client_;
  GcpAuthnFilterConfig config_;
  Http::TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
};

TEST_F(GcpAuthnFilterTest, Success) {
  setupMockObjects();
  // Create the client object.
  createClient();

  client_->fetchToken(request_callbacks_, buildRequest("GET", config_.http_uri().uri()));
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

  EXPECT_CALL(request_callbacks_, onComplete_(response.get()));
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

  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_, httpAsyncClient()).Times(0);
  EXPECT_CALL(request_callbacks_, onComplete_(nullptr));
  GcpAuthnFilterConfig config;
  TestUtility::loadFromYaml(no_cluster_config, config);
  createClient(config);
  client_->fetchToken(request_callbacks_, buildRequest("GET", config.http_uri().uri()));
}

TEST_F(GcpAuthnFilterTest, Failure) {
  setupMockObjects();
  // Create the client object.
  createClient();
  EXPECT_CALL(request_callbacks_, onComplete_(nullptr));
  client_->fetchToken(request_callbacks_, buildRequest("GET", config_.http_uri().uri()));
  client_callback_->onFailure(client_request_, Http::AsyncClient::FailureReason::Reset);
}

TEST_F(GcpAuthnFilterTest, ResumeFilterChain) {
  setupMockObjects();
  auto filter = std::make_shared<GcpAuthnFilter>(config_, context_);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);
  // decodeHeaders() is expected to return `StopIteration` because none of complete functions(i.e.,
  // onSuccess, onFailure, onDestroy, etc) has been called.
  EXPECT_EQ(filter->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopIteration);
  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(GcpAuthnFilterTest, DestoryFilter) {
  setupMockObjects();

  auto filter = std::make_shared<GcpAuthnFilter>(config_, context_);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);
  const std::string upstream_cluster("cluster_0");
  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(testing::ReturnRef(upstream_cluster));
  // decodeHeaders() is expected to return `StopIteration` and state is expected to be in `Calling`
  // state because none of complete functions(i.e., onSuccess, onFailure, onDestroy, etc) has been
  // called.
  EXPECT_EQ(filter->decodeHeaders(default_headers_, true),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(filter->getState(), GcpAuthnFilter::State::Calling);
  filter->onDestroy();
  // onDestroy() call updated the state from `Calling` to `Complete`.
  EXPECT_EQ(filter->getState(), GcpAuthnFilter::State::Complete);
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
