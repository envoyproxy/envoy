#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "common/common/macros.h"

#include "extensions/filters/http/well_known_names.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

using testing::AssertionResult;
using testing::Not;
using testing::TestWithParam;
using testing::ValuesIn;

namespace Envoy {

using Headers = std::vector<std::pair<const std::string, const std::string>>;

class ExtAuthzGrpcIntegrationTest : public Grpc::VersionedGrpcClientIntegrationParamTest,
                                    public HttpIntegrationTest {
public:
  ExtAuthzGrpcIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz");
      ext_authz_cluster->mutable_http2_protocol_options();

      TestUtility::loadFromYaml(base_filter_config_, proto_config_);
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_authz",
                     fake_upstreams_.back()->localAddress());

      proto_config_.mutable_filter_enabled()->set_runtime_key("envoy.ext_authz.enable");
      proto_config_.mutable_filter_enabled()->mutable_default_value()->set_numerator(100);
      proto_config_.mutable_deny_at_disable()->set_runtime_key("envoy.ext_authz.deny_at_disable");
      proto_config_.mutable_deny_at_disable()->mutable_default_value()->set_value(false);
      proto_config_.set_transport_api_version(apiVersion());

      envoy::config::listener::v3::Filter ext_authz_filter;
      ext_authz_filter.set_name(Extensions::HttpFilters::HttpFilterNames::get().ExtAuthorization);
      ext_authz_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.addFilter(MessageUtil::getJsonStringFromMessage(ext_authz_filter));
    });
  }

  void setDenyAtDisableRuntimeConfig(bool deny_at_disable) {
    config_helper_.addRuntimeOverride("envoy.ext_authz.enable", "numerator: 0");
    if (deny_at_disable) {
      config_helper_.addRuntimeOverride("envoy.ext_authz.deny_at_disable", "true");
    } else {
      config_helper_.addRuntimeOverride("envoy.ext_authz.deny_at_disable", "false");
    }
  }

  void initiateClientConnection(uint64_t request_body_length,
                                const Headers& headers_to_add = Headers{},
                                const Headers& headers_to_append = Headers{}) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{
        {":method", "POST"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

    // Initialize headers to append. If the authorization server returns any matching keys with one
    // of value in headers_to_add, the header entry from authorization server replaces the one in
    // headers_to_add.
    for (const auto& header_to_add : headers_to_add) {
      headers.addCopy(header_to_add.first, header_to_add.second);
    }

    // Initialize headers to append. If the authorization server returns any matching keys with one
    // of value in headers_to_append, it will be appended.
    for (const auto& headers_to_append : headers_to_append) {
      headers.addCopy(headers_to_append.first, headers_to_append.second);
    }

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

    EXPECT_EQ("POST", ext_authz_request_->headers().getMethodValue());
    EXPECT_EQ(TestUtility::getVersionedMethodPath("envoy.service.auth.{}.Authorization", "Check",
                                                  apiVersion()),
              ext_authz_request_->headers().getPathValue());
    EXPECT_EQ("application/grpc", ext_authz_request_->headers().getContentTypeValue());

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

  void waitForSuccessfulUpstreamResponse(
      const std::string& expected_response_code, const Headers& headers_to_add = Headers{},
      const Headers& headers_to_append = Headers{},
      const Http::TestRequestHeaderMapImpl& new_headers_from_upstream =
          Http::TestRequestHeaderMapImpl{},
      const Http::TestRequestHeaderMapImpl& headers_to_append_multiple =
          Http::TestRequestHeaderMapImpl{}) {
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(response_size_, true);

    for (const auto& header_to_add : headers_to_add) {
      EXPECT_THAT(upstream_request_->headers(),
                  Http::HeaderValueOf(header_to_add.first, header_to_add.second));
      // For headers_to_add (with append = false), the original request headers have no "-replaced"
      // suffix, but the ones from the authorization server have it.
      EXPECT_TRUE(absl::EndsWith(header_to_add.second, "-replaced"));
    }

    for (const auto& header_to_append : headers_to_append) {
      // The current behavior of appending is using the "appendCopy", which ALWAYS combines entries
      // with the same key into one key, and the values are separated by "," (regardless it is an
      // inline-header or not). In addition to that, it only applies to the existing headers (the
      // header is existed in the original request headers).
      EXPECT_THAT(
          upstream_request_->headers(),
          Http::HeaderValueOf(
              header_to_append.first,
              // In this test, the keys and values of the original request headers have the same
              // string value. Hence for "header2" key, the value is "header2,header2-appended".
              absl::StrCat(header_to_append.first, ",", header_to_append.second)));
      const auto value = upstream_request_->headers()
                             .get(Http::LowerCaseString(header_to_append.first))
                             ->value()
                             .getStringView();
      EXPECT_TRUE(absl::EndsWith(value, "-appended"));
      const auto values = StringUtil::splitToken(value, ",");
      EXPECT_EQ(2, values.size());
    }

    if (!new_headers_from_upstream.empty()) {
      // new_headers_from_upstream has append = true. The current implementation ignores to set
      // multiple headers that are not present in the original request headers. In order to add
      // headers with the same key multiple times, setting response headers with append = false and
      // append = true is required.
      EXPECT_THAT(new_headers_from_upstream,
                  Not(Http::IsSubsetOfHeaders(upstream_request_->headers())));
    }

    if (!headers_to_append_multiple.empty()) {
      // headers_to_append_multiple has append = false for the first entry of multiple entries, and
      // append = true for the rest entries.
      EXPECT_THAT(upstream_request_->headers(),
                  Http::HeaderValueOf("multiple", "multiple-first,multiple-second"));
    }

    response_->waitForEndStream();

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_body_.length(), upstream_request_->bodyLength());

    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(expected_response_code, response_->headers().getStatusValue());
    EXPECT_EQ(response_size_, response_->body().size());
  }

  void sendExtAuthzResponse(const Headers& headers_to_add, const Headers& headers_to_append,
                            const Http::TestRequestHeaderMapImpl& new_headers_from_upstream,
                            const Http::TestRequestHeaderMapImpl& headers_to_append_multiple) {
    ext_authz_request_->startGrpcStream();
    envoy::service::auth::v3::CheckResponse check_response;
    check_response.mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);

    for (const auto& header_to_add : headers_to_add) {
      auto* entry = check_response.mutable_ok_response()->mutable_headers()->Add();
      entry->mutable_append()->set_value(false);
      entry->mutable_header()->set_key(header_to_add.first);
      entry->mutable_header()->set_value(header_to_add.second);
    }

    for (const auto& header_to_append : headers_to_append) {
      auto* entry = check_response.mutable_ok_response()->mutable_headers()->Add();
      entry->mutable_append()->set_value(true);
      entry->mutable_header()->set_key(header_to_append.first);
      entry->mutable_header()->set_value(header_to_append.second);
    }

    // Entries in this headers are not present in the original request headers.
    new_headers_from_upstream.iterate(
        [&check_response](const Http::HeaderEntry& h) -> Http::HeaderMap::Iterate {
          auto* entry = check_response.mutable_ok_response()->mutable_headers()->Add();
          // Try to append to a non-existent field.
          entry->mutable_append()->set_value(true);
          entry->mutable_header()->set_key(std::string(h.key().getStringView()));
          entry->mutable_header()->set_value(std::string(h.value().getStringView()));
          return Http::HeaderMap::Iterate::Continue;
        });

    // Entries in this headers are not present in the original request headers. But we set append =
    // true and append = false.
    headers_to_append_multiple.iterate(
        [&check_response](const Http::HeaderEntry& h) -> Http::HeaderMap::Iterate {
          auto* entry = check_response.mutable_ok_response()->mutable_headers()->Add();
          const auto key = std::string(h.key().getStringView());
          const auto value = std::string(h.value().getStringView());

          // This scenario makes sure we have set the headers to be appended later.
          entry->mutable_append()->set_value(!absl::EndsWith(value, "-first"));
          entry->mutable_header()->set_key(key);
          entry->mutable_header()->set_value(value);
          return Http::HeaderMap::Iterate::Continue;
        });

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
    expectCheckRequestWithBodyWithHeaders(downstream_protocol, request_size, Headers{}, Headers{},
                                          Http::TestRequestHeaderMapImpl{},
                                          Http::TestRequestHeaderMapImpl{});
  }

  void expectCheckRequestWithBodyWithHeaders(
      Http::CodecClient::Type downstream_protocol, uint64_t request_size,
      const Headers& headers_to_add, const Headers& headers_to_append,
      const Http::TestRequestHeaderMapImpl& new_headers_from_upstream,
      const Http::TestRequestHeaderMapImpl& headers_to_append_multiple) {
    initializeConfig();
    setDownstreamProtocol(downstream_protocol);
    HttpIntegrationTest::initialize();
    initiateClientConnection(request_size, headers_to_add, headers_to_append);
    waitForExtAuthzRequest(expectedCheckRequest(downstream_protocol));

    Headers updated_headers_to_add;
    for (auto& header_to_add : headers_to_add) {
      updated_headers_to_add.push_back(
          std::make_pair(header_to_add.first, header_to_add.second + "-replaced"));
    }
    Headers updated_headers_to_append;
    for (const auto& header_to_append : headers_to_append) {
      updated_headers_to_append.push_back(
          std::make_pair(header_to_append.first, header_to_append.second + "-appended"));
    }
    sendExtAuthzResponse(updated_headers_to_add, updated_headers_to_append,
                         new_headers_from_upstream, headers_to_append_multiple);

    waitForSuccessfulUpstreamResponse("200", updated_headers_to_add, updated_headers_to_append,
                                      new_headers_from_upstream, headers_to_append_multiple);
    cleanup();
  }

  void expectFilterDisableCheck(bool deny_at_disable, const std::string& expected_status) {
    initializeConfig();
    setDenyAtDisableRuntimeConfig(deny_at_disable);
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    HttpIntegrationTest::initialize();
    initiateClientConnection(4);
    if (!deny_at_disable) {
      waitForSuccessfulUpstreamResponse(expected_status);
    }
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
                                    public TestWithParam<Network::Address::IpVersion> {
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
        {"baz", "foo"},
        {"bat", "foo"},
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

    // Send back authorization response with "baz" and "bat" headers.
    // Also add multiple values "append-foo" and "append-bar" for key "x-append-bat".
    Http::TestResponseHeaderMapImpl response_headers{
        {":status", "200"},
        {"baz", "baz"},
        {"bat", "bar"},
        {"x-append-bat", "append-foo"},
        {"x-append-bat", "append-bar"},
    };
    ext_authz_request_->encodeHeaders(response_headers, true);
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

    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    // The original client request header value of "baz" is "foo". Since we configure to "override"
    // the value of "baz", we expect the request headers to be sent to upstream contain only one
    // "baz" with value "baz" (set by the authorization server).
    EXPECT_THAT(upstream_request_->headers(), Http::HeaderValueOf("baz", "baz"));

    // The original client request header value of "bat" is "foo". Since we configure to "append"
    // the value of "bat", we expect the request headers to be sent to upstream contain two "bat"s,
    // with values: "foo" and "bar" (the "bat: bar" header is appended by the authorization server).
    const auto& request_existed_headers =
        Http::TestRequestHeaderMapImpl{{"bat", "foo"}, {"bat", "bar"}};
    EXPECT_THAT(request_existed_headers, Http::IsSubsetOfHeaders(upstream_request_->headers()));

    // The original client request header does not contain x-append-bat. Since we configure to
    // "append" the value of "x-append-bat", we expect the headers to be sent to upstream contain
    // two "x-append-bat"s, instead of replacing the first with the last one, with values:
    // "append-foo" and "append-bar"
    const auto& request_nonexisted_headers = Http::TestRequestHeaderMapImpl{
        {"x-append-bat", "append-foo"}, {"x-append-bat", "append-bar"}};
    EXPECT_THAT(request_nonexisted_headers, Http::IsSubsetOfHeaders(upstream_request_->headers()));

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

    authorization_response:
      allowed_upstream_headers:
        patterns:
        - exact: baz
        - prefix: x-success

      allowed_upstream_headers_to_append:
        patterns:
        - exact: bat
        - prefix: x-append

  failure_mode_allow: true
  )EOF";
};

INSTANTIATE_TEST_SUITE_P(IpVersionsCientType, ExtAuthzGrpcIntegrationTest,
                         VERSIONED_GRPC_CLIENT_INTEGRATION_PARAMS);

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

// Verifies that the original request headers will be added and appended when the authorization
// server returns headers_to_add and headers_to_append in OkResponse message.
TEST_P(ExtAuthzGrpcIntegrationTest, SendHeadersToAddAndToAppendToUpstream) {
  expectCheckRequestWithBodyWithHeaders(
      Http::CodecClient::Type::HTTP1, 4,
      /*headers_to_add=*/Headers{{"header1", "header1"}},
      /*headers_to_append=*/Headers{{"header2", "header2"}},
      /*new_headers_from_upstream=*/Http::TestRequestHeaderMapImpl{{"new1", "new1"}},
      /*headers_to_append_multiple=*/
      Http::TestRequestHeaderMapImpl{{"multiple", "multiple-first"},
                                     {"multiple", "multiple-second"}});
}

TEST_P(ExtAuthzGrpcIntegrationTest, AllowAtDisable) { expectFilterDisableCheck(false, "200"); }

TEST_P(ExtAuthzGrpcIntegrationTest, DenyAtDisable) { expectFilterDisableCheck(true, "403"); }

INSTANTIATE_TEST_SUITE_P(IpVersions, ExtAuthzHttpIntegrationTest,
                         ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that by default HTTP service uses the case-sensitive string matcher.
TEST_P(ExtAuthzHttpIntegrationTest, DefaultCaseSensitiveStringMatcher) {
  setupWithDisabledCaseSensitiveStringMatcher(false);
  const auto* header_entry = ext_authz_request_->headers().get(case_sensitive_header_name_);
  ASSERT_EQ(header_entry, nullptr);
}

// Verifies that by setting "false" to
// envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher, the string
// matcher used by HTTP service will be case-insensitive.
TEST_P(ExtAuthzHttpIntegrationTest, DisableCaseSensitiveStringMatcher) {
  setupWithDisabledCaseSensitiveStringMatcher(true);
  const auto* header_entry = ext_authz_request_->headers().get(case_sensitive_header_name_);
  ASSERT_NE(header_entry, nullptr);
  EXPECT_EQ(case_sensitive_header_value_, header_entry->value().getStringView());
}

} // namespace Envoy
