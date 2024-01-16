#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

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

class ExtAuthzHttpClientTest : public testing::Test {
public:
  ExtAuthzHttpClientTest() : async_request_{&async_client_} { initialize(EMPTY_STRING); }

  void initialize(const std::string& yaml) {
    config_ = createConfig(yaml);
    client_ = std::make_unique<RawHttpClientImpl>(cm_, config_);
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillByDefault(ReturnRef(async_client_));
  }

  ClientConfigSharedPtr createConfig(const std::string& yaml = EMPTY_STRING, uint32_t timeout = 250,
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
            allowed_client_headers_on_success:
              patterns:
              - prefix: "X-Downstream-"
                ignore_case: true
        )EOF";
      TestUtility::loadFromYaml(default_yaml, proto_config);
    } else {
      TestUtility::loadFromYaml(yaml, proto_config);
    }

    cm_.initializeThreadLocalClusters({"ext_authz"});
    return std::make_shared<ClientConfig>(proto_config, timeout, path_prefix);
  }

  void dynamicMetadataTest(CheckStatus status, const std::string& http_status) {
    const std::string yaml = R"EOF(
    http_service:
      server_uri:
        uri: "ext_authz:9000"
        cluster: "ext_authz"
        timeout: 0.25s
      authorization_response:
        dynamic_metadata_from_headers:
          patterns:
          - prefix: "X-Metadata-"
            ignore_case: true
    failure_mode_allow: true
    )EOF";

    initialize(yaml);
    envoy::service::auth::v3::CheckRequest request;
    client_->check(request_callbacks_, request, parent_span_, stream_info_);

    ProtobufWkt::Struct expected_dynamic_metadata;
    auto* metadata_fields = expected_dynamic_metadata.mutable_fields();
    (*metadata_fields)["x-metadata-header-0"] = ValueUtil::stringValue("zero");
    (*metadata_fields)["x-metadata-header-1"] = ValueUtil::stringValue("2");
    (*metadata_fields)["x-metadata-header-2"] = ValueUtil::stringValue("4");

    // When we call onSuccess() at the bottom of the test we expect that all the
    // dynamic metadata values that we set above to be present in the authz Response
    // below.
    Response authz_response;
    authz_response.status = status;
    authz_response.dynamic_metadata = expected_dynamic_metadata;
    EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                        AuthzResponseNoAttributes(authz_response))));

    const HeaderValueOptionVector http_response_headers = TestCommon::makeHeaderValueOption({
        {":status", http_status, false},
        {"bar", "nope", false},
        {"x-metadata-header-0", "zero", false},
        {"x-metadata-header-1", "2", false},
        {"x-foo", "nah", false},
        {"x-metadata-header-2", "4", false},
    });
    Http::ResponseMessagePtr http_response = TestCommon::makeMessageResponse(http_response_headers);
    client_->onSuccess(async_request_, std::move(http_response));
  }

  Http::RequestMessagePtr sendRequest(absl::node_hash_map<std::string, std::string>&& headers,
                                      const std::string body_content = EMPTY_STRING,
                                      bool use_raw_body = false) {
    envoy::service::auth::v3::CheckRequest request{};
    auto mutable_headers =
        request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
    for (const auto& header : headers) {
      (*mutable_headers)[header.first] = header.second;
    }

    if (use_raw_body) {
      *request.mutable_attributes()->mutable_request()->mutable_http()->mutable_raw_body() =
          body_content;
    } else {
      *request.mutable_attributes()->mutable_request()->mutable_http()->mutable_body() =
          body_content;
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

    client_->check(request_callbacks_, request, parent_span_, stream_info_);
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
  Tracing::MockSpan parent_span_;
  Tracing::MockSpan child_span_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// Test HTTP client config default values.
TEST_F(ExtAuthzHttpClientTest, ClientConfig) {
  const Http::LowerCaseString foo{"foo"};
  const Http::LowerCaseString bar{"bar"};
  const Http::LowerCaseString alice{"alice"};

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

// Test default allowed client and upstream headers in the HTTP client.
TEST_F(ExtAuthzHttpClientTest, TestDefaultAllowedClientAndUpstreamHeaders) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
  failure_mode_allow: true
  )EOF";

  initialize(yaml);

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

// Verify request body is set correctly when the normal body is empty and raw body is set.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithRawBody) {
  Http::RequestMessagePtr message_ptr =
      sendRequest({{":path", "/foo"}, {"foo", "bar"}}, "raw_body", true);

  EXPECT_EQ(message_ptr->bodyAsString(), "raw_body");
}

// Verify request body is set correctly when the normal body is set and raw body is empty.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithBody) {
  Http::RequestMessagePtr message_ptr =
      sendRequest({{":path", "/foo"}, {"foo", "bar"}}, "body", false);

  EXPECT_EQ(message_ptr->bodyAsString(), "body");
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

  Http::RequestMessagePtr message_ptr =
      sendRequest({{Http::Headers::get().ContentLength.get(), std::string{"47"}},
                   {Http::Headers::get().Method.get(), std::string{"POST"}}});

  EXPECT_EQ(message_ptr->headers().getContentLengthValue(), "0");
  EXPECT_EQ(message_ptr->headers().getMethodValue(), "POST");
}

// Verify client response when authorization server returns a 200 OK.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOk) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, request, parent_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(async_request_, std::move(check_response));
}

using HeaderValuePair = std::pair<const Http::LowerCaseString, const std::string>;

// Verify client response headers when authorization_headers_to_add is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAddedAuthzHeaders) {
  const auto expected_headers = TestCommon::makeHeaderValueOption(
      {{":status", "200", false}, {"x-downstream-ok", "1", false}, {"x-upstream-ok", "1", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::OK, Http::Code::OK, EMPTY_STRING, TestCommon::makeHeaderValueOption({}),
      // By default, the value of envoy.config.core.v3.HeaderValueOption.append is true.
      TestCommon::makeHeaderValueOption({{"x-downstream-ok", "1", true}}));
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
  client_->check(request_callbacks_, request, parent_span_, stream_info_);

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
  client_->check(request_callbacks_, request, parent_span_, stream_info);

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
  client_->check(request_callbacks_, request, parent_span_, stream_info_);

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
  client_->check(request_callbacks_, request, parent_span_, stream_info_);

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

// Test the client when an OK response is received with dynamic metadata in that OK response.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithDynamicMetadata) {
  dynamicMetadataTest(CheckStatus::OK, "200");
}

// Test the client when a denied response is received with dynamic metadata in the denied response.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedWithDynamicMetadata) {
  dynamicMetadataTest(CheckStatus::Denied, "403");
}

// Test the client when a denied response is received.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDenied) {
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "403", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Forbidden, EMPTY_STRING, expected_headers);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, request, parent_span_, stream_info_);

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
  client_->check(request_callbacks_, request, parent_span_, stream_info_);

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
          {{"x-foo", "bar", false}, {":status", "401", false}, {"foo", "bar", false}}),
      TestCommon::makeHeaderValueOption({}));

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, request, parent_span_, stream_info_);
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

  client_->check(request_callbacks_, request, parent_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(async_request_, Http::AsyncClient::FailureReason::Reset);
}

// Test the client when a call to authorization server returns a 5xx error status.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequest5xxError) {
  Http::ResponseMessagePtr check_response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
  envoy::service::auth::v3::CheckRequest request;

  client_->check(request_callbacks_, request, parent_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onSuccess(async_request_, std::move(check_response));
}

// Test the client when the request is canceled.
TEST_F(ExtAuthzHttpClientTest, CancelledAuthorizationRequest) {
  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, request, parent_span_, stream_info_);

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

// Test the client when the configured cluster is missing/removed.
TEST_F(ExtAuthzHttpClientTest, NoCluster) {
  InSequence s;

  EXPECT_CALL(cm_, getThreadLocalCluster(Eq("ext_authz"))).WillOnce(Return(nullptr));
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->check(request_callbacks_, envoy::service::auth::v3::CheckRequest{}, parent_span_,
                 stream_info_);
}

} // namespace
} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
