#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "extensions/filters/http/well_known_names.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

class ExtAuthzGrpcIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                    public HttpIntegrationTest {
public:
  ExtAuthzGrpcIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initializeWithDownstreamProtocol(Http::CodecClient::Type downstream_protocol) {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz");
      ext_authz_cluster->mutable_http2_protocol_options();

      TestUtility::loadFromYaml(base_filter_config_, proto_config_);
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_authz",
                     fake_upstreams_.back()->localAddress());

      envoy::config::listener::v3::Filter ext_authz_filter;
      ext_authz_filter.set_name(Extensions::HttpFilters::HttpFilterNames::get().ExtAuthorization);
      ext_authz_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.addFilter(MessageUtil::getJsonStringFromMessage(ext_authz_filter));
    });

    setDownstreamProtocol(downstream_protocol);
    HttpIntegrationTest::initialize();
  }

  void initiateClientConnection(uint64_t request_body_length) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{
        {":method", "POST"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};
    TestUtility::feedBufferWithRandomCharacters(request_body_, request_body_length);
    response_ = codec_client_->makeRequestWithBody(headers, request_body_.toString());
  }

  void waitForExtAuthzRequest(const std::string& expected_check_request_yaml) {
    AssertionResult result =
        fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, fake_ext_authz_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request_);
    RELEASE_ASSERT(result, result.message());

    // Check for the validity of the received CheckRequest.
    envoy::service::auth::v3::CheckRequest check_request;
    result = ext_authz_request_->waitForGrpcMessage(*dispatcher_, check_request);
    RELEASE_ASSERT(result, result.message());

    EXPECT_EQ("POST", ext_authz_request_->headers().Method()->value().getStringView());
    EXPECT_EQ("/envoy.service.auth.v2.Authorization/Check",
              ext_authz_request_->headers().Path()->value().getStringView());
    EXPECT_EQ("application/grpc",
              ext_authz_request_->headers().ContentType()->value().getStringView());

    envoy::service::auth::v3::CheckRequest expected_check_request;
    TestUtility::loadFromYaml(expected_check_request_yaml, expected_check_request);

    auto* attributes = check_request.mutable_attributes();
    auto* http_request = attributes->mutable_request()->mutable_http();

    EXPECT_TRUE(attributes->request().has_time());

    // Clear fields which are not relevant.
    attributes->clear_source();
    attributes->clear_destination();
    attributes->clear_metadata_context();
    attributes->mutable_request()->clear_time();
    http_request->clear_id();
    http_request->clear_headers();
    http_request->clear_scheme();

    EXPECT_EQ(check_request.DebugString(), expected_check_request.DebugString());

    result = ext_authz_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
  }

  void waitForSuccessfulUpstreamResponse() {
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(response_size_, true);
    response_->waitForEndStream();

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_body_.length(), upstream_request_->bodyLength());

    EXPECT_TRUE(response_->complete());
    EXPECT_EQ("200", response_->headers().Status()->value().getStringView());
    EXPECT_EQ(response_size_, response_->body().size());
  }

  void sendExtAuthzResponse() {
    ext_authz_request_->startGrpcStream();
    envoy::service::auth::v3::CheckResponse check_response;
    check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
    ext_authz_request_->sendGrpcMessage(check_response);
    ext_authz_request_->finishGrpcStream(Grpc::Status::Ok);
  }

  const std::string expectedRequestBody() {
    const std::string request_body_string = request_body_.toString();
    const uint64_t request_body_length = request_body_.length();
    return request_body_length > max_request_bytes_
               ? request_body_string.substr(0, max_request_bytes_)
               : request_body_string;
  }

  void cleanup() {
    if (fake_ext_authz_connection_ != nullptr) {
      if (clientType() != Grpc::ClientType::GoogleGrpc) {
        AssertionResult result = fake_ext_authz_connection_->close();
        RELEASE_ASSERT(result, result.message());
      }
      AssertionResult result = fake_ext_authz_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    cleanupUpstreamAndDownstream();
  }

  const std::string expectedCheckRequest(Http::CodecClient::Type downstream_protocol) {
    const std::string expected_downstream_protocol =
        downstream_protocol == Http::CodecClient::Type::HTTP1 ? "HTTP/1.1" : "HTTP/2";
    constexpr absl::string_view expected_format = R"EOF(
attributes:
  request:
    http:
      method: POST
      path: /test
      host: host
      size: "%d"
      body: "%s"
      protocol: %s
)EOF";

    return absl::StrFormat(expected_format, request_body_.length(), expectedRequestBody(),
                           expected_downstream_protocol);
  }

  void expectCheckRequestWithBody(Http::CodecClient::Type downstream_protocol,
                                  uint64_t request_size) {
    initializeWithDownstreamProtocol(downstream_protocol);
    initiateClientConnection(request_size);
    waitForExtAuthzRequest(expectedCheckRequest(downstream_protocol));
    sendExtAuthzResponse();
    waitForSuccessfulUpstreamResponse();
    cleanup();
  }

  FakeHttpConnectionPtr fake_ext_authz_connection_;
  FakeStreamPtr ext_authz_request_;
  IntegrationStreamDecoderPtr response_;

  Buffer::OwnedImpl request_body_;
  const uint64_t response_size_ = 512;
  const uint64_t max_request_bytes_ = 1024;
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config_{};
  const std::string base_filter_config_ = R"EOF(
    with_request_body:
      max_request_bytes: 1024
      allow_partial_message: true
  )EOF";
};

class ExtAuthzHttpIntegrationTest : public HttpIntegrationTest,
                                    public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ExtAuthzHttpIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  }

  // By default, HTTP Service uses case sensitive string matcher.
  void disableCaseSensitiveStringMatcher() {
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher",
        "false");
  }

  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    response_ = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "http"},
        {":authority", "host"},
        {"x-case-sensitive-header", case_sensitive_header_value_},
    });
  }

  void waitForExtAuthzRequest() {
    AssertionResult result =
        fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, fake_ext_authz_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request_);
    RELEASE_ASSERT(result, result.message());
    result = ext_authz_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
  }

  void cleanup() {
    if (fake_ext_authz_connection_ != nullptr) {
      AssertionResult result = fake_ext_authz_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_ext_authz_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    cleanupUpstreamAndDownstream();
  }

  void setupWithDisabledCaseSensitiveStringMatcher(bool disable_case_sensitive_matcher) {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz");

      TestUtility::loadFromYaml(default_config_, proto_config_);

      envoy::config::listener::v3::Filter ext_authz_filter;
      ext_authz_filter.set_name(Extensions::HttpFilters::HttpFilterNames::get().ExtAuthorization);
      ext_authz_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.addFilter(MessageUtil::getJsonStringFromMessage(ext_authz_filter));
    });

    if (disable_case_sensitive_matcher) {
      disableCaseSensitiveStringMatcher();
    }

    HttpIntegrationTest::initialize();

    initiateClientConnection();
    waitForExtAuthzRequest();

    response_->waitForEndStream();
    EXPECT_TRUE(response_->complete());

    cleanup();
  }

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config_{};
  FakeHttpConnectionPtr fake_ext_authz_connection_;
  FakeStreamPtr ext_authz_request_;
  IntegrationStreamDecoderPtr response_;
  const Http::LowerCaseString case_sensitive_header_name_{"x-case-sensitive-header"};
  const std::string case_sensitive_header_value_{"Case-Sensitive"};
  const std::string default_config_ = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
      allowed_headers:
        patterns:
        - exact: X-Case-Sensitive-Header
  failure_mode_allow: true
  )EOF";
};

INSTANTIATE_TEST_SUITE_P(IpVersionsCientType, ExtAuthzGrpcIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/1.1.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP1DownstreamRequestWithBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP1, 4);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/1.1 and the size of the request body is larger than max_request_bytes.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP1DownstreamRequestWithLargeBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP1, 2048);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/2.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP2DownstreamRequestWithBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP2, 4);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/2 and the size of the request body is larger than max_request_bytes.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP2DownstreamRequestWithLargeBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP2, 2048);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ExtAuthzHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that by default HTTP service uses the case sensitive string matcher.
TEST_P(ExtAuthzHttpIntegrationTest, DefaultCaseSensitiveStringMatcher) {
  setupWithDisabledCaseSensitiveStringMatcher(false);
  const auto* header_entry = ext_authz_request_->headers().get(case_sensitive_header_name_);
  ASSERT_EQ(header_entry, nullptr);
}

// Verifies that by setting "false" to
// envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher, the string
// matcher used by HTTP service will case insensitive.
TEST_P(ExtAuthzHttpIntegrationTest, DisableCaseSensitiveStringMatcher) {
  setupWithDisabledCaseSensitiveStringMatcher(true);
  const auto* header_entry = ext_authz_request_->headers().get(case_sensitive_header_name_);
  ASSERT_NE(header_entry, nullptr);
  EXPECT_EQ(case_sensitive_header_value_, header_entry->value().getStringView());
}

} // namespace
} // namespace Envoy
