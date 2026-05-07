#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/extensions/filters/http/gcp_authn/jwt_gcp_authn_client_impl.h"

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
using testing::Return;
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

class JwtGcpAuthnClientImplTest : public testing::Test {
public:
  JwtGcpAuthnClientImplTest() {
    // Initialize the default configuration.
    TestUtility::loadFromYaml(DefaultConfig, config_);
  }

  void setupMockObjects() {
    EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
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

  void createClient() { client_ = std::make_unique<JwtGcpAuthnClientImpl>(config_, context_); }

  NiceMock<MockFactoryContext> context_;
  NiceMock<MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<Envoy::Http::MockAsyncClientRequest> client_request_{
      &thread_local_cluster_.async_client_};
  NiceMock<MockGcpAuthnClientCallbacks> request_callbacks_;

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* client_callback_;
  Envoy::Http::RequestMessagePtr message_;
  Envoy::Http::AsyncClient::RequestOptions options_;

  std::unique_ptr<JwtGcpAuthnClientImpl> client_;
  GcpAuthnFilterConfig config_;
};

TEST_F(JwtGcpAuthnClientImplTest, Success) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchToken(audience, absl::nullopt, request_callbacks_);
  EXPECT_EQ(message_->headers().Method()->value().getStringView(), "GET");
  EXPECT_EQ(message_->headers().Path()->value().getStringView(),
            "/computeMetadata/v1/instance/service-accounts/default/identity?audience=http://test_audience");

  EXPECT_EQ(options_.retry_policy->num_retries().value(), 5);
  EXPECT_EQ(options_.retry_policy->retry_back_off().base_interval().seconds(), 1);
  EXPECT_EQ(options_.retry_policy->retry_back_off().max_interval().seconds(), 10);
  EXPECT_EQ(options_.retry_policy->retry_on(), "5xx,gateway-error,connect-failure,reset");

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add("token_string");

  EXPECT_CALL(request_callbacks_, onComplete(absl::StatusOr<std::string>("token_string")));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(JwtGcpAuthnClientImplTest, NoCluster) {
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

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              httpAsyncClient())
      .Times(0);

  EXPECT_CALL(request_callbacks_, onComplete(_));
  GcpAuthnFilterConfig config;
  TestUtility::loadFromYaml(no_cluster_config, config);
  config_ = config;
  createClient();
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchToken(audience, absl::nullopt, request_callbacks_);
}

TEST_F(JwtGcpAuthnClientImplTest, Failure) {
  setupMockObjects();
  createClient();
  EXPECT_CALL(request_callbacks_, onComplete(_));
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchToken(audience, absl::nullopt, request_callbacks_);
  client_callback_->onFailure(client_request_, Http::AsyncClient::FailureReason::Reset);
}

TEST_F(JwtGcpAuthnClientImplTest, NotOkResponse) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchToken(audience, absl::nullopt, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr resp_headers(new Envoy::Http::TestResponseHeaderMapImpl({
      {":status", "504"},
  }));
  Envoy::Http::ResponseMessagePtr response(
      new Envoy::Http::ResponseMessageImpl(std::move(resp_headers)));
  EXPECT_CALL(request_callbacks_, onComplete(_));
  client_callback_->onSuccess(client_request_, std::move(response));
}

TEST_F(JwtGcpAuthnClientImplTest, EmptyResponseHeader) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchToken(audience, absl::nullopt, request_callbacks_);

  Envoy::Http::ResponseHeaderMapPtr empty_resp_headers(
      new Envoy::Http::TestResponseHeaderMapImpl({}));
  Envoy::Http::ResponseMessagePtr empty_response(
      new Envoy::Http::ResponseMessageImpl(std::move(empty_resp_headers)));
  EXPECT_CALL(request_callbacks_, onComplete(_));
  client_callback_->onSuccess(client_request_, std::move(empty_response));
}

TEST_F(JwtGcpAuthnClientImplTest, Cancel) {
  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchToken(audience, absl::nullopt, request_callbacks_);

  EXPECT_CALL(client_request_, cancel());
  client_->cancel();
}

TEST_F(JwtGcpAuthnClientImplTest, NoRetryPolicy) {
  std::string no_retry_config = R"EOF(
    http_uri:
      uri: http://testhost/path/test
      cluster: test_cluster
      timeout:
        seconds: 5
  )EOF";

  GcpAuthnFilterConfig config;
  TestUtility::loadFromYaml(no_retry_config, config);
  config_ = config;

  setupMockObjects();
  createClient();

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://test_audience");
  client_->fetchToken(audience, absl::nullopt, request_callbacks_);

  EXPECT_FALSE(options_.retry_policy.has_value());
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
