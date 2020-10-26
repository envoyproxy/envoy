#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "common/common/empty_string.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::Eq;
using testing::InSequence;
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

constexpr uint32_t REQUEST_TIMEOUT{250};

class ExtAuthzHttpClientTest : public testing::Test {
public:
  ExtAuthzHttpClientTest() : async_request_{&async_client_} { initialize(EMPTY_STRING); }

  void initialize(const std::string& yaml) {
    config_ = createConfig(yaml);
    client_ = std::make_unique<RawHttpClientImpl>(cm_, config_);
    ON_CALL(cm_, httpAsyncClientForCluster(config_->cluster()))
        .WillByDefault(ReturnRef(async_client_));
  }

  ClientConfigSharedPtr createConfig(const std::string& yaml = EMPTY_STRING,
                                     uint32_t timeout = REQUEST_TIMEOUT,
                                     const std::string& path_prefix = "/bar") {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    if (yaml.empty()) {
      const std::string default_yaml = R"EOF(
        http_service:
          server_uri:
            uri: "ext_authz:9000"
            cluster: "ext_authz"
            timeout: 0.25s

          authorization_request:
            allowed_headers:
              patterns:
              - exact: Baz
                ignore_case: true
              - prefix: "X-"
                ignore_case: true
              - safe_regex:
                  google_re2: {}
                  regex: regex-foo.?
            headers_to_add:
            - key: "x-authz-header1"
              value: "value"
            - key: "x-authz-header2"
              value: "value"

          authorization_response:
            allowed_upstream_headers:
              patterns:
              - exact: Bar
                ignore_case: true
              - prefix: "X-"
                ignore_case: true
            allowed_upstream_headers_to_append:
              patterns:
              - exact: Alice
                ignore_case: true
              - prefix: "Append-"
                ignore_case: true
            allowed_client_headers:
              patterns:
              - exact: Foo
                ignore_case: true
              - prefix: "X-"
                ignore_case: true
        )EOF";
      TestUtility::loadFromYaml(default_yaml, proto_config);
    } else {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    return std::make_shared<ClientConfig>(proto_config, timeout, path_prefix);
  }

  Http::RequestMessagePtr sendRequest(absl::node_hash_map<std::string, std::string>&& headers) {
    envoy::service::auth::v3::CheckRequest request{};
    auto mutable_headers =
        request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
    for (const auto& header : headers) {
      (*mutable_headers)[header.first] = header.second;
    }

    Http::RequestMessagePtr message_ptr;
    EXPECT_CALL(async_client_, send_(_, _, _))
        .WillOnce(Invoke(
            [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                const Envoy::Http::AsyncClient::RequestOptions) -> Http::AsyncClient::Request* {
              message_ptr = std::move(message);
              return nullptr;
            }));

    const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
    const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
    auto check_response = TestCommon::makeMessageResponse(expected_headers);

    client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);
    EXPECT_CALL(request_callbacks_,
                onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
    client_->onSuccess(async_request_, std::move(check_response));

    return message_ptr;
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Http::MockAsyncClientRequest> async_request_;
  ClientConfigSharedPtr config_;
  std::unique_ptr<RawHttpClientImpl> client_;
  MockRequestCallbacks request_callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Tracing::MockSpan parent_span_;
  Tracing::MockSpan child_span_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// Test HTTP client config default values.
TEST_F(ExtAuthzHttpClientTest, ClientConfig) {
  const Http::LowerCaseString foo{"foo"};
  const Http::LowerCaseString baz{"baz"};
  const Http::LowerCaseString bar{"bar"};
  const Http::LowerCaseString alice{"alice"};

  // Check allowed request headers.
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(
      config_->requestHeaderMatchers()->matches(Http::CustomHeaders::get().Authorization.get()));
  EXPECT_FALSE(config_->requestHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(baz.get()));

  // Check allowed client headers.
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Status.get()));
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));
  EXPECT_FALSE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Path.get()));
  EXPECT_FALSE(config_->clientHeaderMatchers()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(Http::Headers::get().WWWAuthenticate.get()));
  EXPECT_FALSE(config_->clientHeaderMatchers()->matches(Http::CustomHeaders::get().Origin.get()));
  EXPECT_TRUE(config_->clientHeaderMatchers()->matches(foo.get()));

  // Check allowed upstream headers.
  EXPECT_TRUE(config_->upstreamHeaderMatchers()->matches(bar.get()));

  // Check allowed upstream headers to append.
  EXPECT_TRUE(config_->upstreamHeaderToAppendMatchers()->matches(alice.get()));

  // Check other attributes.
  EXPECT_EQ(config_->pathPrefix(), "/bar");
  EXPECT_EQ(config_->cluster(), "ext_authz");
  EXPECT_EQ(config_->tracingName(), "async ext_authz egress");
  EXPECT_EQ(config_->timeout(), std::chrono::milliseconds{250});
}

// Test default allowed headers in the HTTP client.
TEST_F(ExtAuthzHttpClientTest, TestDefaultAllowedHeaders) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
  failure_mode_allow: true
  )EOF";

  initialize(yaml);

  // Check allowed request headers.
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Host.get()));
  EXPECT_TRUE(
      config_->requestHeaderMatchers()->matches(Http::CustomHeaders::get().Authorization.get()));
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
  Http::RequestMessagePtr message_ptr = sendRequest({{":path", "/foo"}, {"foo", "bar"}});

  EXPECT_EQ(message_ptr->headers().getPathValue(), "/bar/foo");
}

// Test the client when a request contains Content-Length greater than 0.
TEST_F(ExtAuthzHttpClientTest, ContentLengthEqualZero) {
  Http::RequestMessagePtr message_ptr =
      sendRequest({{Http::Headers::get().ContentLength.get(), std::string{"47"}},
                   {Http::Headers::get().Method.get(), std::string{"POST"}}});

  EXPECT_EQ(message_ptr->headers().getContentLengthValue(), "0");
  EXPECT_EQ(message_ptr->headers().getMethodValue(), "POST");
}

// Test the client when a request contains Content-Length greater than 0.
TEST_F(ExtAuthzHttpClientTest, ContentLengthEqualZeroWithAllowedHeaders) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
      allowed_headers:
        patterns:
        - exact: content-length
  failure_mode_allow: true
  )EOF";

  initialize(yaml);
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().ContentLength.get()));

  Http::RequestMessagePtr message_ptr =
      sendRequest({{Http::Headers::get().ContentLength.get(), std::string{"47"}},
                   {Http::Headers::get().Method.get(), std::string{"POST"}}});

  EXPECT_EQ(message_ptr->headers().getContentLengthValue(), "0");
  EXPECT_EQ(message_ptr->headers().getMethodValue(), "POST");
}

// Test the client when a request contains headers in the prefix matchers.
TEST_F(ExtAuthzHttpClientTest, AllowedRequestHeadersPrefix) {
  const Http::LowerCaseString regexFood{"regex-food"};
  const Http::LowerCaseString regexFool{"regex-fool"};
  Http::RequestMessagePtr message_ptr =
      sendRequest({{Http::Headers::get().XContentTypeOptions.get(), "foobar"},
                   {Http::Headers::get().XSquashDebug.get(), "foo"},
                   {Http::Headers::get().ContentType.get(), "bar"},
                   {regexFood.get(), "food"},
                   {regexFool.get(), "fool"}});

  EXPECT_TRUE(message_ptr->headers().get(Http::Headers::get().ContentType).empty());
  const auto x_squash = message_ptr->headers().get(Http::Headers::get().XSquashDebug);
  ASSERT_FALSE(x_squash.empty());
  EXPECT_EQ(x_squash[0]->value().getStringView(), "foo");

  const auto x_content_type = message_ptr->headers().get(Http::Headers::get().XContentTypeOptions);
  ASSERT_FALSE(x_content_type.empty());
  EXPECT_EQ(x_content_type[0]->value().getStringView(), "foobar");

  const auto food = message_ptr->headers().get(regexFood);
  ASSERT_FALSE(food.empty());
  EXPECT_EQ(food[0]->value().getStringView(), "food");

  const auto fool = message_ptr->headers().get(regexFool);
  ASSERT_FALSE(fool.empty());
  EXPECT_EQ(fool[0]->value().getStringView(), "fool");
}

// Verify client response when authorization server returns a 200 OK.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOk) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(async_request_, std::move(check_response));
}

using HeaderValuePair = std::pair<const Http::LowerCaseString, const std::string>;

// Verify client response headers when authorization_headers_to_add is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAddedAuthzHeaders) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v3::CheckRequest request;
  auto mutable_headers =
      request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
  (*mutable_headers)[std::string{":x-authz-header2"}] = std::string{"forged-value"};
  // Expect that header1 will be added and header2 correctly overwritten. Due to this behavior, the
  // append property of header value option should always be false.
  const HeaderValuePair header1{"x-authz-header1", "value"};
  const HeaderValuePair header2{"x-authz-header2", "value"};
  EXPECT_CALL(async_client_,
              send_(AllOf(ContainsPairAsHeader(header1), ContainsPairAsHeader(header2)), _, _));
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  // Check for child span tagging when the request is allowed.
  EXPECT_CALL(child_span_, setTag(Eq("ext_authz_http_status"), Eq("OK")));
  EXPECT_CALL(child_span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  client_->onBeforeFinalizeUpstreamSpan(child_span_, &check_response->headers());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(async_request_, std::move(check_response));
}

// Verify client response headers when authorization_headers_to_add is configured with value from
// stream info.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAddedAuthzHeadersFromStreamInfo) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
      headers_to_add:
      - key: "x-authz-header1"
        value: "%REQ(x-request-id)%"
  failure_mode_allow: true
  )EOF";

  initialize(yaml);

  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);

  const HeaderValuePair expected_header{"x-authz-header1", "123"};
  EXPECT_CALL(async_client_, send_(ContainsPairAsHeader(expected_header), _, _));

  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.addCopy(Http::LowerCaseString(std::string("x-request-id")),
                          expected_header.second);

  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(stream_info, getRequestHeaders()).WillOnce(Return(&request_headers));

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(async_request_, std::move(check_response));
}

// Verify client response headers when allow_upstream_headers is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAllowHeader) {
  const std::string empty_body{};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"x-baz", "foo", false}, {"bar", "foo", false}});
  const auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  const auto check_response_headers =
      TestCommon::makeHeaderValueOption({{":status", "200", false},
                                         {":path", "/bar", false},
                                         {":method", "post", false},
                                         {"content-length", "post", false},
                                         {"bar", "foo", false},
                                         {"x-baz", "foo", false},
                                         {"foobar", "foo", false}});

  auto message_response = TestCommon::makeMessageResponse(check_response_headers);
  client_->onSuccess(async_request_, std::move(message_response));
}

// Verify headers present in x-envoy-auth-headers-to-remove make it into the
// Response correctly.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithHeadersToRemove) {
  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  // When we call onSuccess() at the bottom of the test we expect that all the
  // headers-to-remove in that http response to have been correctly extracted
  // and inserted into the authz Response just below.
  Response authz_response;
  authz_response.status = CheckStatus::OK;
  authz_response.headers_to_remove.emplace_back(Http::LowerCaseString{"remove-me"});
  authz_response.headers_to_remove.emplace_back(Http::LowerCaseString{"remove-me-too"});
  authz_response.headers_to_remove.emplace_back(Http::LowerCaseString{"remove-me-also"});
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));

  const HeaderValueOptionVector http_response_headers = TestCommon::makeHeaderValueOption({
      {":status", "200", false},
      {"x-envoy-auth-headers-to-remove", " ,remove-me,, ,  remove-me-too , ", false},
      {"x-envoy-auth-headers-to-remove", " remove-me-also ", false},
  });
  Http::ResponseMessagePtr http_response = TestCommon::makeMessageResponse(http_response_headers);
  client_->onSuccess(async_request_, std::move(http_response));
}

// Test the client when a denied response is received.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDenied) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "403", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Forbidden, EMPTY_STRING, expected_headers);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  // Check for child span tagging when the request is denied.
  EXPECT_CALL(child_span_, setTag(Eq("ext_authz_http_status"), Eq("Forbidden")));
  EXPECT_CALL(child_span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  client_->onBeforeFinalizeUpstreamSpan(child_span_, &check_response->headers());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));
  client_->onSuccess(async_request_, TestCommon::makeMessageResponse(expected_headers));
}

// Verify client response headers and body when the authorization server denies the request.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedWithAllAttributes) {
  const auto expected_body = std::string{"test"};
  const auto expected_headers = TestCommon::makeHeaderValueOption(
      {{":status", "401", false}, {"foo", "bar", false}, {"x-foobar", "bar", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));
  client_->onSuccess(async_request_,
                     TestCommon::makeMessageResponse(expected_headers, expected_body));
}

// Verify client response headers when the authorization server denies the request and
// allowed_client_headers is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedAndAllowedClientHeaders) {
  const auto expected_body = std::string{"test"};
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body,
      TestCommon::makeHeaderValueOption(
          {{"x-foo", "bar", false}, {":status", "401", false}, {"foo", "bar", false}}));

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));
  const auto check_response_headers = TestCommon::makeHeaderValueOption({{":method", "post", false},
                                                                         {"x-foo", "bar", false},
                                                                         {":status", "401", false},
                                                                         {"foo", "bar", false}});
  client_->onSuccess(async_request_,
                     TestCommon::makeMessageResponse(check_response_headers, expected_body));
}

// Test the client when an unknown error occurs.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequestError) {
  envoy::service::auth::v3::CheckRequest request;

  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(async_request_, Http::AsyncClient::FailureReason::Reset);
}

// Test the client when a call to authorization server returns a 5xx error status.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequest5xxError) {
  Http::ResponseMessagePtr check_response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
  envoy::service::auth::v3::CheckRequest request;

  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onSuccess(async_request_, std::move(check_response));
}

// Test the client when the request is canceled.
TEST_F(ExtAuthzHttpClientTest, CancelledAuthorizationRequest) {
  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

// Test the client when the request times out on an internal timeout.
TEST_F(ExtAuthzHttpClientTest, AuthorizationInternalRequestTimeout) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.ext_authz_measure_timeout_on_check_created", "true"}});

  initialize("");
  envoy::service::auth::v3::CheckRequest request;

  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(REQUEST_TIMEOUT), _));

  EXPECT_CALL(async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  EXPECT_CALL(async_request_, cancel());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzTimedoutResponse())));
  timer->invokeCallback();
}

// Test when the client is cancelled with internal timeout.
TEST_F(ExtAuthzHttpClientTest, AuthorizationInternalRequestTimeoutCancelled) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.ext_authz_measure_timeout_on_check_created", "true"}});

  initialize("");
  envoy::service::auth::v3::CheckRequest request;

  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(REQUEST_TIMEOUT), _));

  EXPECT_CALL(async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, dispatcher_, request, parent_span_, stream_info_);

  // make sure cancel resets the timer:
  EXPECT_CALL(async_request_, cancel());
  bool timer_destroyed = false;
  timer->timer_destroyed_ = &timer_destroyed;
  client_->cancel();
  EXPECT_EQ(timer_destroyed, true);
}

// Test the client when the configured cluster is missing/removed.
TEST_F(ExtAuthzHttpClientTest, NoCluster) {
  InSequence s;

  EXPECT_CALL(cm_, get(Eq("ext_authz"))).WillOnce(Return(nullptr));
  EXPECT_CALL(cm_, httpAsyncClientForCluster("ext_authz")).Times(0);
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->check(request_callbacks_, dispatcher_, envoy::service::auth::v3::CheckRequest{},
                 parent_span_, stream_info_);
}

} // namespace
} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
