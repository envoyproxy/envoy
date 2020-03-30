#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "common/common/empty_string.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

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

using LoadDnsCacheEntryStatus =
    Envoy::Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;
using MockLoadDnsCacheEntryResult =
    Envoy::Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult;

class ExtAuthzHttpClientTest
    : public testing::Test,
      public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  ExtAuthzHttpClientTest() {}

  void initialize(const std::string& yaml = EMPTY_STRING) {
    config_ = createConfig(yaml);
    client_ = std::make_unique<RawHttpClientImpl>(cm_, config_, time_source_);
    ON_CALL(cm_, httpAsyncClientForCluster(config_->clusterName()))
        .WillByDefault(ReturnRef(cm_.async_client_));
  }

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz
  createConfigWithDnsCache(const std::string& cluster_name) {
    // Prepare a configuration which uses "dns_cache_config",
    //    http_service:
    //      server_uri:
    //        uri: "http://localhost:3000/verify"
    //        cluster: service_dynamic_forward_proxy
    //        timeout: 1s
    //      dns_cache_config:
    //        name: dynamic_forward_proxy_cache_config
    //        dns_lookup_family: V4_ONLY
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config;
    auto* server_uri = proto_config.mutable_http_service()->mutable_server_uri();
    server_uri->set_uri("http://ext-authz.io:9000/auth");
    server_uri->set_cluster(cluster_name);
    server_uri->mutable_timeout()->set_seconds(1);

    auto* dns_cache = proto_config.mutable_http_service()->mutable_dns_cache_config();
    dns_cache->set_name("dynamic_forward_proxy_cache_config");
    dns_cache->set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_ONLY);

    return proto_config;
  }

  ClientConfigSharedPtr createConfig(const std::string& yaml, uint32_t timeout = 250,
                                     const std::string& path_prefix = "/prefix") {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config;
    if (yaml.empty()) {
      const std::string default_yaml = R"EOF(
        http_service:
          server_uri:
            uri: "http://ext-authz.io:9000"
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
    proto_config.mutable_http_service()->set_path_prefix(path_prefix);
    return std::make_shared<ClientConfig>(proto_config, timeout, *this);
  }

  Http::RequestMessagePtr sendRequest(
      std::unordered_map<std::string, std::string>&& headers,
      LoadDnsCacheEntryStatus load_dns_cache_entry_status = LoadDnsCacheEntryStatus::InCache) {
    Http::RequestMessagePtr message_ptr;
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke(
            [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                const Envoy::Http::AsyncClient::RequestOptions) -> Http::AsyncClient::Request* {
              message_ptr = std::move(message);
              return nullptr;
            }));

    envoy::service::auth::v3::CheckRequest request;
    auto mutable_headers =
        request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
    for (const auto& header : headers) {
      (*mutable_headers)[header.first] = header.second;
    }

    client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

    if (load_dns_cache_entry_status == LoadDnsCacheEntryStatus::Loading) {
      client_->onLoadDnsCacheComplete();
    }

    const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
    const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
    auto check_response = TestCommon::makeMessageResponse(expected_headers);
    EXPECT_CALL(request_callbacks_,
                onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));

    client_->onSuccess(async_request_, std::move(check_response));
    return message_ptr;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockAsyncClientRequest> async_request_{&cm_.async_client_};
  ClientConfigSharedPtr config_;
  TimeSource& time_source_{cm_.async_client_.dispatcher().timeSource()};
  std::unique_ptr<RawHttpClientImpl> client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan active_span_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// Test initializing the HTTP client with an invalid server URI value.
TEST_F(ExtAuthzHttpClientTest, TestInvalidServerUri) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "invalid"
      cluster: "ext_authz"
      timeout: 0.25s
  failure_mode_allow: true
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(initialize(yaml), EnvoyException,
                            "server URI 'invalid' is not a valid URI");
}

// Test HTTP client config default values.
TEST_F(ExtAuthzHttpClientTest, ClientConfig) {
  initialize();

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
  EXPECT_EQ(config_->pathPrefix(), "/prefix");
  EXPECT_EQ(config_->clusterName(), "ext_authz");
  EXPECT_EQ(config_->tracingName(), "async ext_authz egress");
  EXPECT_EQ(config_->timeout(), std::chrono::milliseconds{250});
}

// Test default allowed headers in the HTTP client.
TEST_F(ExtAuthzHttpClientTest, TestDefaultAllowedHeaders) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "http://ext-authz.io:9000"
      cluster: "ext_authz"
      timeout: 0.25s
  failure_mode_allow: true
  )EOF";

  initialize(yaml);

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
  initialize();

  Http::RequestMessagePtr message_ptr = sendRequest({{":path", "/foo"}, {"foo", "bar"}});

  const auto* path = message_ptr->headers().get(Http::Headers::get().Path);
  ASSERT_NE(path, nullptr);
  // The path_prefix is "/prefix", and the path configured in server_uri.uri is "/"
  // (http://ext-authz.io:9000 is parsed with "/" as path).
  EXPECT_EQ(path->value().getStringView(), "/prefix/");
}

// Test the client when a request contains Content-Length greater than 0.
TEST_F(ExtAuthzHttpClientTest, ContentLengthEqualZero) {
  initialize();

  Http::RequestMessagePtr message_ptr =
      sendRequest({{Http::Headers::get().ContentLength.get(), std::string{"47"}},
                   {Http::Headers::get().Method.get(), std::string{"POST"}}});

  const auto* content_length = message_ptr->headers().get(Http::Headers::get().ContentLength);
  ASSERT_NE(content_length, nullptr);
  EXPECT_EQ(content_length->value().getStringView(), "0");

  const auto* method = message_ptr->headers().get(Http::Headers::get().Method);
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(method->value().getStringView(), "POST");
}

// Test the client when a request contains Content-Length greater than 0.
TEST_F(ExtAuthzHttpClientTest, ContentLengthEqualZeroWithAllowedHeaders) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "http://ext-authz.io:9000"
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

  const auto* content_length = message_ptr->headers().get(Http::Headers::get().ContentLength);
  ASSERT_NE(content_length, nullptr);
  EXPECT_EQ(content_length->value().getStringView(), "0");

  const auto* method = message_ptr->headers().get(Http::Headers::get().Method);
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(method->value().getStringView(), "POST");
}

// Test the client when a request contains headers in the prefix matchers.
TEST_F(ExtAuthzHttpClientTest, AllowedRequestHeadersPrefix) {
  initialize();

  const Http::LowerCaseString regexFood{"regex-food"};
  const Http::LowerCaseString regexFool{"regex-fool"};
  Http::RequestMessagePtr message_ptr =
      sendRequest({{Http::Headers::get().XContentTypeOptions.get(), "foobar"},
                   {Http::Headers::get().XSquashDebug.get(), "foo"},
                   {Http::Headers::get().ContentType.get(), "bar"},
                   {regexFood.get(), "food"},
                   {regexFool.get(), "fool"}});

  EXPECT_EQ(message_ptr->headers().get(Http::Headers::get().ContentType), nullptr);
  const auto* x_squash = message_ptr->headers().get(Http::Headers::get().XSquashDebug);
  ASSERT_NE(x_squash, nullptr);
  EXPECT_EQ(x_squash->value().getStringView(), "foo");

  const auto* x_content_type = message_ptr->headers().get(Http::Headers::get().XContentTypeOptions);
  ASSERT_NE(x_content_type, nullptr);
  EXPECT_EQ(x_content_type->value().getStringView(), "foobar");

  const auto* food = message_ptr->headers().get(regexFood);
  ASSERT_NE(food, nullptr);
  EXPECT_EQ(food->value().getStringView(), "food");

  const auto* fool = message_ptr->headers().get(regexFool);
  ASSERT_NE(fool, nullptr);
  EXPECT_EQ(fool->value().getStringView(), "fool");
}

// Verify client response when authorization server returns a 200 OK.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOk) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));

  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("OK")));
  EXPECT_CALL(*child_span, finishSpan());
  client_->onSuccess(async_request_, std::move(check_response));
}

using HeaderValuePair = std::pair<const Http::LowerCaseString, const std::string>;

// Verify client response headers when authorization_headers_to_add is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAddedAuthzHeaders) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);
  envoy::service::auth::v3::CheckRequest request;
  auto mutable_headers =
      request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
  (*mutable_headers)[std::string{":x-authz-header2"}] = std::string{"forged-value"};

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));
  // Expect that header1 will be added and header2 correctly overwritten. Due to this behavior, the
  // append property of header value option should always be false.
  const HeaderValuePair header1{"x-authz-header1", "value"};
  const HeaderValuePair header2{"x-authz-header2", "value"};
  EXPECT_CALL(cm_.async_client_,
              send_(AllOf(ContainsPairAsHeader(header1), ContainsPairAsHeader(header2)), _, _));
  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("OK")));
  EXPECT_CALL(*child_span, finishSpan());
  client_->onSuccess(async_request_, std::move(check_response));
}

// Verify client response headers when authorization_headers_to_add is configured with value from
// stream info.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAddedAuthzHeadersFromStreamInfo) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "http://ext-authz.io:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
      headers_to_add:
      - key: "x-authz-header1"
        value: "%REQ(x-request-id)%"
  failure_mode_allow: true
  )EOF";

  initialize(yaml);

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "200", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));

  const HeaderValuePair expected_header{"x-authz-header1", "123"};
  EXPECT_CALL(cm_.async_client_, send_(ContainsPairAsHeader(expected_header), _, _));

  Http::RequestHeaderMapImpl request_headers;
  request_headers.addCopy(Http::LowerCaseString(std::string("x-request-id")),
                          expected_header.second);

  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(stream_info, getRequestHeaders()).WillOnce(Return(&request_headers));

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, request, active_span_, stream_info);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("OK")));
  EXPECT_CALL(*child_span, finishSpan());
  client_->onSuccess(async_request_, std::move(check_response));
}

// Verify client response headers when allow_upstream_headers is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationOkWithAllowHeader) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  const std::string empty_body{};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"x-baz", "foo", false}, {"bar", "foo", false}});
  const auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));
  client_->check(request_callbacks_, request, active_span_, stream_info_);

  const auto check_response_headers = TestCommon::makeHeaderValueOption({{":status", "200", false},
                                                                         {":path", "/bar", false},
                                                                         {":method", "post", false},
                                                                         {"bar", "foo", false},
                                                                         {"x-baz", "foo", false},
                                                                         {"foobar", "foo", false}});

  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("OK")));
  EXPECT_CALL(*child_span, finishSpan());
  auto message_response = TestCommon::makeMessageResponse(check_response_headers);
  client_->onSuccess(async_request_, std::move(message_response));
}

// Test the client when a denied response is received.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDenied) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{":status", "403", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Forbidden, EMPTY_STRING, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));
  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("Forbidden")));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));
  client_->onSuccess(async_request_, TestCommon::makeMessageResponse(expected_headers));
}

// Verify client response headers and body when the authorization server denies the request.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedWithAllAttributes) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  const auto expected_body = std::string{"test"};
  const auto expected_headers = TestCommon::makeHeaderValueOption(
      {{":status", "401", false}, {"foo", "bar", false}, {"x-foobar", "bar", false}});
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body, expected_headers);

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("Unauthorized")));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));
  client_->onSuccess(async_request_,
                     TestCommon::makeMessageResponse(expected_headers, expected_body));
}

// Verify client response headers when the authorization server denies the request and
// allowed_client_headers is configured.
TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedAndAllowedClientHeaders) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  const auto expected_body = std::string{"test"};
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body,
      TestCommon::makeHeaderValueOption(
          {{"x-foo", "bar", false}, {":status", "401", false}, {"foo", "bar", false}}));

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));

  envoy::service::auth::v3::CheckRequest request;
  client_->check(request_callbacks_, request, active_span_, stream_info_);
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("Unauthorized")));
  EXPECT_CALL(*child_span, finishSpan());
  const auto check_response_headers = TestCommon::makeHeaderValueOption({{":method", "post", false},
                                                                         {"x-foo", "bar", false},
                                                                         {":status", "401", false},
                                                                         {"foo", "bar", false}});
  client_->onSuccess(async_request_,
                     TestCommon::makeMessageResponse(check_response_headers, expected_body));
}

// Test the client when an unknown error occurs.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequestError) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));

  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());
  client_->onFailure(async_request_, Http::AsyncClient::FailureReason::Reset);
}

// Test the client when a call to authorization server returns a 5xx error status.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequest5xxError) {
  initialize();

  Http::ResponseMessagePtr check_response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));

  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  EXPECT_CALL(*child_span, setTag(Eq("ext_authz_http_status"), Eq("Service Unavailable")));
  EXPECT_CALL(*child_span, finishSpan());
  client_->onSuccess(async_request_, std::move(check_response));
}

// Test the client when a call to authorization server returns a status code that cannot be
// parsed.
TEST_F(ExtAuthzHttpClientTest, AuthorizationRequestErrorParsingStatusCode) {
  initialize();

  Http::ResponseMessagePtr check_response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "foo"}}}));
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));

  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());
  client_->onSuccess(async_request_, std::move(check_response));
}

// Test the client when the request is canceled.
TEST_F(ExtAuthzHttpClientTest, CancelledAuthorizationRequest) {
  initialize();

  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  envoy::service::auth::v3::CheckRequest request;

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(*child_span, injectContext(_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, request, active_span_, stream_info_);

  EXPECT_CALL(async_request_, cancel());
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Status), Eq(Tracing::Tags::get().Canceled)));
  EXPECT_CALL(*child_span, finishSpan());
  client_->cancel();
}

// Test the client when the configured cluster is missing/removed.
TEST_F(ExtAuthzHttpClientTest, NoCluster) {
  initialize();

  InSequence s;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};

  EXPECT_CALL(active_span_, spawnChild_(_, config_->tracingName(), _)).WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(config_->clusterName())));
  EXPECT_CALL(cm_, get(Eq("ext_authz"))).WillOnce(Return(nullptr));
  EXPECT_CALL(cm_, httpAsyncClientForCluster("ext_authz")).Times(0);
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());
  client_->check(request_callbacks_, envoy::service::auth::v3::CheckRequest{}, active_span_,
                 stream_info_);
}

// Test forwarding original path.
TEST_F(ExtAuthzHttpClientTest, TestForwardingRequestPath) {
  const std::string yaml = R"EOF(
  http_service:
    server_uri:
      uri: "http://ext-authz.io:9000/auth"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
      allowed_headers:
        patterns:
        - exact: ":path" # we are forwarding the downstream request path to the authorization
                         # server.
          ignore_case: true
  )EOF";

  initialize(yaml);
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Method.get()));
  EXPECT_TRUE(config_->requestHeaderMatchers()->matches(Http::Headers::get().Path.get()));

  Http::RequestMessagePtr message_ptr =
      sendRequest({{Http::Headers::get().Method.get(), std::string{"POST"}},
                   {Http::Headers::get().Path.get(), std::string{"/original"}}});

  // The check-request's path to the authorization server is as specified by server_uri.uri.
  const auto* path = message_ptr->headers().get(Http::Headers::get().Path);
  ASSERT_NE(nullptr, path);
  // In this test setup, the check-request's path is prefixed by "/prefix" by default.
  EXPECT_EQ("/prefix/auth", path->value().getStringView());

  // While the downstream request path is preserved as x-envoy-original-path.
  const auto* original_path = message_ptr->headers().get(Http::Headers::get().EnvoyOriginalPath);
  ASSERT_NE(nullptr, original_path);
  EXPECT_EQ("/original", original_path->value().getStringView());

  const auto* method = message_ptr->headers().get(Http::Headers::get().Method);
  ASSERT_NE(nullptr, method);
  EXPECT_EQ("POST", method->value().getStringView());
}

TEST_F(ExtAuthzHttpClientTest, TestSendingRequestToDynamicClusterInLoading) {
  const std::string cluster_name = "dynamic_forward_proxy_cluster";
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  ON_CALL(cm_, httpAsyncClientForCluster(cluster_name)).WillByDefault(ReturnRef(cm_.async_client_));

  EXPECT_CALL(*dns_cache_manager_, getCache(_));
  EXPECT_CALL(cm_, get(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("ext-authz.io:9000"), 9000, _))
      .WillOnce(Return(MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Loading, handle}));
  EXPECT_CALL(*handle, onDestroy());

  const auto proto_config = createConfigWithDnsCache(cluster_name);
  config_ = std::make_shared<ClientConfig>(proto_config, 1000, *this);
  client_ = std::make_unique<RawHttpClientImpl>(cm_, config_, time_source_);

  sendRequest({{":path", "/foo"}, {"foo", "bar"}}, LoadDnsCacheEntryStatus::Loading);
}

TEST_F(ExtAuthzHttpClientTest, TestSendingRequestToDynamicClusterInCache) {
  const std::string cluster_name = "dynamic_forward_proxy_cluster";
  ON_CALL(cm_, httpAsyncClientForCluster(cluster_name)).WillByDefault(ReturnRef(cm_.async_client_));

  EXPECT_CALL(*dns_cache_manager_, getCache(_));
  EXPECT_CALL(cm_, get(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("ext-authz.io:9000"), 9000, _))
      .WillOnce(Return(MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::InCache, nullptr}));

  const auto proto_config = createConfigWithDnsCache(cluster_name);
  config_ = std::make_shared<ClientConfig>(proto_config, 1000, *this);
  client_ = std::make_unique<RawHttpClientImpl>(cm_, config_, time_source_);

  sendRequest({{":path", "/foo"}, {"foo", "bar"}}, LoadDnsCacheEntryStatus::InCache);
}

TEST_F(ExtAuthzHttpClientTest, TestSendingRequestToDynamicClusterOverflow) {
  const std::string cluster_name = "dynamic_forward_proxy_cluster";
  ON_CALL(cm_, httpAsyncClientForCluster(cluster_name)).WillByDefault(ReturnRef(cm_.async_client_));

  EXPECT_CALL(*dns_cache_manager_, getCache(_));
  EXPECT_CALL(cm_, get(_));
  EXPECT_CALL(*dns_cache_manager_->dns_cache_, loadDnsCacheEntry_(Eq("ext-authz.io:9000"), 9000, _))
      .WillOnce(Return(MockLoadDnsCacheEntryResult{LoadDnsCacheEntryStatus::Overflow, nullptr}));

  const auto proto_config = createConfigWithDnsCache(cluster_name);
  config_ = std::make_shared<ClientConfig>(proto_config, 1000, *this);
  client_ = std::make_unique<RawHttpClientImpl>(cm_, config_, time_source_);

  std::unordered_map<std::string, std::string> headers{{":path", "/foo"}, {"foo", "bar"}};
  envoy::service::auth::v3::CheckRequest request;
  auto mutable_headers =
      request.mutable_attributes()->mutable_request()->mutable_http()->mutable_headers();
  for (const auto& header : headers) {
    (*mutable_headers)[header.first] = header.second;
  }

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));

  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);
}

} // namespace
} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
