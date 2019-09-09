#include "envoy/api/v2/core/base.pb.h"

#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {
namespace {

class ExtAuthzHttpClientTest : public testing::Test {
public:
  ExtAuthzHttpClientTest()
      : async_request_{&async_client_}, config_{createConfig()}, client_{cm_, config_} {
    ON_CALL(cm_, httpAsyncClientForCluster(config_->cluster()))
        .WillByDefault(ReturnRef(async_client_));
  }

  static ClientConfigSharedPtr createConfig(std::string yaml = "", uint32_t timeout = 250,
                                            std::string path_prefix = "/bar") {
    envoy::config::filter::http::ext_authz::v2::ExtAuthz proto_config{};
    if (yaml.empty()) {
      std::string default_yaml = R"EOF(
        http_service:
          server_uri:
            uri: "ext_authz:9000"
            cluster: "ext_authz"
            timeout: 0.25s

          authorization_request:
            allowed_headers:
              patterns:
              - exact: Baz
              - prefix: "x-"
            headers_to_add:
            - key: "x-authz-header1"
              value: "value"
            - key: "x-authz-header2"
              value: "value"

          authorization_response:
            allowed_upstream_headers:
              patterns:
              - exact: Bar
              - prefix: "x-"
            allowed_client_headers:
              patterns:
              - exact: Foo
              - prefix: "x-"
        )EOF";
      TestUtility::loadFromYaml(default_yaml, proto_config);
    } else {
      TestUtility::loadFromYaml(yaml, proto_config);
    }

    return std::make_shared<ClientConfig>(ClientConfig{proto_config, timeout, path_prefix});
  }

  Http::MessagePtr sendRequest(std::unordered_map<std::string, std::string>&& headers) {
    envoy::service::auth::v2::CheckRequest request{};
    auto mutable_headers =
        request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
    for (const auto& header : headers) {
      (*mutable_headers)[header.first] = header.second;
    }

    Http::MessagePtr message_ptr;
    EXPECT_CALL(async_client_, send_(_, _, _))
        .WillOnce(Invoke(
            [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks&,
                const Envoy::Http::AsyncClient::RequestOptions) -> Http::AsyncClient::Request* {
              message_ptr = std::move(message);
              return nullptr;
            }));

    const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
    const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
    auto check_response = TestCommon::makeMessageResponse(expected_headers);

    client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
    EXPECT_CALL(request_callbacks_,
                onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
    client_.onSuccess(std::move(check_response));

    return message_ptr;
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Http::MockAsyncClientRequest> async_request_;
  ClientConfigSharedPtr config_;
  RawHttpClientImpl client_;
  MockRequestCallbacks request_callbacks_;
};

// Test HTTP client config default values.
TEST_F(ExtAuthzHttpClientTest, ClientConfig) {
  const Http::LowerCaseString foo{"foo"};
  const Http::LowerCaseString baz{"baz"};
  const Http::LowerCaseString bar{"bar"};

  // Check allowed request headers.
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Authorization.get()));
  EXPECT_FALSE(config_->requestHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(baz.get()));

  // // Check allowed client headers.
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Status.get()));
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));
  EXPECT_FALSE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Path.get()));
  EXPECT_FALSE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(Http::Headers::get().WWWAuthenticate.get()));
  EXPECT_FALSE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Origin.get()));
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(foo.get()));

  // // Check allowed upstream headers.
  EXPECT_TRUE(config_->upstreamHeaderMatchers()->matches(bar.get()));

  // // Check other attributes.
  EXPECT_EQ(config_->pathPrefix(), "/bar");
  EXPECT_EQ(config_->cluster(), "ext_authz");
  EXPECT_EQ(config_->timeout(), std::chrono::milliseconds{250});
}

// // Test default allowed headers in the HTTP client.
TEST_F(ExtAuthzHttpClientTest, TestDefaultAllowedHeaders) {
  std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
  failure_mode_allow: true
  )EOF";

  const auto config_ = createConfig(yaml);
  const auto key = Http::LowerCaseString{"key"};

  // Check allowed request headers.
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Authorization.get()));
  EXPECT_FALSE(config_->requestHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));

  // Check allowed client headers.
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));
  EXPECT_FALSE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Host.get()));

  // Check allowed upstream headers.
  EXPECT_FALSE(
      config_->upstreamHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));
}

// Verify client response when the authorization server returns a 200 OK and path_prefix is
// configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithPathRewrite) {
  Http::MessagePtr message_ptr = sendRequest({{":path", "/foo"}, {"foo", "bar"}});

  const auto* path = message_ptr->headers().get(Http::Headers::get().Path);
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(path->value().getStringView(), "/bar/foo");
}

// Test the client when a request contains Content-Length greater than 0.
TEST_F(ExtAuthzHttpClientTest, ContentLengthEqualZero) {
  Http::MessagePtr message_ptr =
      sendRequest({{Http::Headers::get().ContentLength.get(), std::string{"47"}},
                   {Http::Headers::get().Method.get(), std::string{"POST"}}});

  const auto* content_length = message_ptr->headers().get(Http::Headers::get().ContentLength);
  ASSERT_NE(content_length, nullptr);
  EXPECT_EQ(content_length->value().getStringView(), "0");

  const auto* method = message_ptr->headers().get(Http::Headers::get().Method);
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(method->value().getStringView(), "POST");
}

// Test the client when a request contains headers in the prefix matchers.
TEST_F(ExtAuthzHttpClientTest, AllowedRequestHeadersPrefix) {
  Http::MessagePtr message_ptr =
      sendRequest({{Http::Headers::get().XContentTypeOptions.get(), "foobar"},
                   {Http::Headers::get().XSquashDebug.get(), "foo"},
                   {Http::Headers::get().ContentType.get(), "bar"}});

  EXPECT_EQ(message_ptr->headers().get(Http::Headers::get().ContentType), nullptr);
  const auto* x_squash = message_ptr->headers().get(Http::Headers::get().XSquashDebug);
  ASSERT_NE(x_squash, nullptr);
  EXPECT_EQ(x_squash->value().getStringView(), "foo");

  const auto* x_content_type = message_ptr->headers().get(Http::Headers::get().XContentTypeOptions);
  ASSERT_NE(x_content_type, nullptr);
  EXPECT_EQ(x_content_type->value().getStringView(), "foobar");
}

// Verify client response when authorization server returns a 200 OK.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOk) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v2::CheckRequest request;

  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));

  client_.onSuccess(std::move(check_response));
}

// Verify client response headers when authorization_headers_to_add is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAddedAuthzHeaders) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v2::CheckRequest request;
  auto mutable_headers =
      request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
  (*mutable_headers)[std::string{":x-authz-header2"}] = std::string{"forged-value"};

  // Expect that header1 will be added and header2 correctly overwritten.
  EXPECT_CALL(async_client_, send_(AllOf(ContainsPairAsHeader(config_->headersToAdd().front()),
                                         ContainsPairAsHeader(config_->headersToAdd().back())),
                                   _, _));
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_.onSuccess(std::move(check_response));
}

// Verify client response headers when allow_upstream_headers is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAllowHeader) {
  const std::string empty_body{};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"x-baz", "foo", false}, {"bar", "foo", false}});
  const auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers);

  envoy::service::auth::v2::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));

  const auto check_response_headers =
      TestCommon::makeHeaderValueOption({{":status", "200", false},
                                         {":path", "/bar", false},
                                         {":method", "post", false},
                                         {"content-length", "post", false},
                                         {"bar", "foo", false},
                                         {"x-baz", "foo", false},
                                         {"foobar", "foo", false}});
  auto message_response = TestCommon::makeMessageResponse(check_response_headers);
  client_.onSuccess(std::move(message_response));
}

// Test the client when a denied response is received.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDenied) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "403", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Forbidden, "", expected_headers);

  envoy::service::auth::v2::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(TestCommon::makeMessageResponse(expected_headers));
}

// Verify client response headers and body when the authorization server denies the request.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedWithAllAttributes) {
  const auto expected_body = std::string{"test"};
  const auto expected_headers = TestCommon::makeHeaderValueOption(
      {{":status", "401", false}, {"foo", "bar", false}, {"x-foobar", "bar", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body, expected_headers);

  envoy::service::auth::v2::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(TestCommon::makeMessageResponse(expected_headers, expected_body));
}

// Verify client response headers when the authorization server denies the request and
// allowed_client_headers is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedAndAllowedClientHeaders) {
  const auto expected_body = std::string{"test"};
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body,
      TestCommon::makeHeaderValueOption(
          {{"x-foo", "bar", false}, {":status", "401", false}, {"foo", "bar", false}}));

  envoy::service::auth::v2::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  const auto check_response_headers = TestCommon::makeHeaderValueOption({{":method", "post", false},
                                                                         {"x-foo", "bar", false},
                                                                         {":status", "401", false},
                                                                         {"foo", "bar", false}});
  client_.onSuccess(TestCommon::makeMessageResponse(check_response_headers, expected_body));
}

// Test the client when an unknown error occurs.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequestError) {
  envoy::service::auth::v2::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_.onFailure(Http::AsyncClient::FailureReason::Reset);
}

// Test the client when a call to authorization server returns a 5xx error status.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequest5xxError) {
  Http::MessagePtr check_response(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}}));
  envoy::service::auth::v2::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));

  client_.onSuccess(std::move(check_response));
}

// Test the client when a call to authorization server returns a status code that cannot be
// parsed.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequestErrorParsingStatusCode) {
  Http::MessagePtr check_response(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "foo"}}}));
  envoy::service::auth::v2::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));

  client_.onSuccess(std::move(check_response));
}

// Test the client when the request is canceled.
TEST_F(ExtAuthzHttpClientTest, CancelledAuthorizationRequest) {
  envoy::service::auth::v2::CheckRequest request;
  EXPECT_CALL(async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

} // namespace
} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
