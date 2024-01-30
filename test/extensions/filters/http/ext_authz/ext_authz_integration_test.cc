#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/common/macros.h"
#include "source/server/config_validation/server.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/mocks/server/options.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

using testing::AssertionResult;
using testing::Not;
using testing::TestWithParam;
using testing::ValuesIn;

namespace Envoy {

using Headers = std::vector<std::pair<const std::string, const std::string>>;

class ExtAuthzGrpcIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                    public HttpIntegrationTest {
public:
  ExtAuthzGrpcIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, ExtAuthzGrpcIntegrationTest::ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initializeConfig(bool disable_with_metadata = false, bool failure_mode_allow = false) {
    config_helper_.addConfigModifier([this, disable_with_metadata, failure_mode_allow](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz_cluster");
      ConfigHelper::setHttp2(*ext_authz_cluster);

      TestUtility::loadFromYaml(base_filter_config_, proto_config_);
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_authz_cluster",
                     fake_upstreams_.back()->localAddress());

      proto_config_.mutable_filter_enabled()->set_runtime_key("envoy.ext_authz.enable");
      proto_config_.mutable_filter_enabled()->mutable_default_value()->set_numerator(100);
      proto_config_.set_bootstrap_metadata_labels_key("labels");
      if (disable_with_metadata) {
        // Disable the ext_authz filter with metadata matcher that never matches.
        auto* metadata = proto_config_.mutable_filter_enabled_metadata();
        metadata->set_filter("xyz.abc");
        metadata->add_path()->set_key("k1");
        metadata->mutable_value()->mutable_string_match()->set_exact("never_matched");
      }
      proto_config_.mutable_deny_at_disable()->set_runtime_key("envoy.ext_authz.deny_at_disable");
      proto_config_.mutable_deny_at_disable()->mutable_default_value()->set_value(false);
      proto_config_.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);

      proto_config_.set_failure_mode_allow(failure_mode_allow);

      // Add labels and verify they are passed.
      std::map<std::string, std::string> labels;
      labels["label_1"] = "value_1";
      labels["label_2"] = "value_2";

      ProtobufWkt::Struct metadata;
      ProtobufWkt::Value val;
      ProtobufWkt::Struct* labels_obj = val.mutable_struct_value();
      for (const auto& pair : labels) {
        ProtobufWkt::Value val;
        val.set_string_value(pair.second);
        (*labels_obj->mutable_fields())[pair.first] = val;
      }
      (*metadata.mutable_fields())["labels"] = val;

      *bootstrap.mutable_node()->mutable_metadata() = metadata;

      envoy::config::listener::v3::Filter ext_authz_filter;
      ext_authz_filter.set_name("envoy.filters.http.ext_authz");
      ext_authz_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_authz_filter));
    });
  }

  void setDenyAtDisableRuntimeConfig(bool deny_at_disable, bool disable_with_metadata) {
    if (!disable_with_metadata) {
      config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
        layer->set_name("enable layer");
        ProtobufWkt::Struct& runtime = *layer->mutable_static_layer();
        bootstrap.mutable_layered_runtime()->mutable_layers(0)->set_name("base layer");

        ProtobufWkt::Struct& enable =
            *(*runtime.mutable_fields())["envoy.ext_authz.enable"].mutable_struct_value();
        (*enable.mutable_fields())["numerator"].set_number_value(0);
      });
    }
    if (deny_at_disable) {
      config_helper_.addRuntimeOverride("envoy.ext_authz.deny_at_disable", "true");
    } else {
      config_helper_.addRuntimeOverride("envoy.ext_authz.deny_at_disable", "false");
    }
  }

  void initiateClientConnection(uint64_t request_body_length,
                                const Headers& headers_to_add = Headers{},
                                const Headers& headers_to_append = Headers{},
                                const Headers& headers_to_remove = Headers{}) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{
        {":method", "POST"},           {":path", "/test"},
        {":scheme", "http"},           {":authority", "host"},
        {"x-duplicate", "one"},        {"x-duplicate", "two"},
        {"x-duplicate", "three"},      {"allowed-prefix-one", "one"},
        {"allowed-prefix-two", "two"}, {"not-allowed", "nope"},
        {"regex-food", "food"},        {"regex-fool", "fool"}};

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

    // Initialize headers to be removed. If the authorization server returns any of
    // these as a header to remove, it will be removed.
    for (const auto& header_to_remove : headers_to_remove) {
      headers.addCopy(header_to_remove.first, header_to_remove.second);
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
    EXPECT_EQ("/envoy.service.auth.v3.Authorization/Check",
              ext_authz_request_->headers().getPathValue());
    EXPECT_EQ("application/grpc", ext_authz_request_->headers().getContentTypeValue());

    envoy::service::auth::v3::CheckRequest expected_check_request;
    TestUtility::loadFromYaml(expected_check_request_yaml, expected_check_request);

    auto* attributes = check_request.mutable_attributes();
    auto* http_request = attributes->mutable_request()->mutable_http();

    EXPECT_TRUE(attributes->request().has_time());
    EXPECT_EQ("value_1", attributes->destination().labels().at("label_1"));
    EXPECT_EQ("value_2", attributes->destination().labels().at("label_2"));

    // verify headers in check request, making sure that duplicate headers
    // are merged.
    EXPECT_EQ("one", (*http_request->mutable_headers())["allowed-prefix-one"]);
    EXPECT_EQ("two", (*http_request->mutable_headers())["allowed-prefix-two"]);
    EXPECT_EQ("one,two,three", (*http_request->mutable_headers())["x-duplicate"]);
    EXPECT_EQ("food", (*http_request->mutable_headers())["regex-food"]);
    EXPECT_EQ("fool", (*http_request->mutable_headers())["regex-fool"]);
    EXPECT_FALSE(http_request->headers().contains("not-allowed"));

    // Clear fields which are not relevant.
    attributes->clear_source();
    attributes->clear_destination();
    attributes->clear_metadata_context();
    attributes->clear_route_metadata_context();
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
      const Headers& headers_to_append = Headers{}, const Headers& headers_to_remove = Headers{},
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

    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"},
                                        {"replaceable", "set-by-upstream"},
                                        {"set-cookie", "cookie1=snickerdoodle"}},
        false);
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
                             .get(Http::LowerCaseString(header_to_append.first))[0]
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

    for (const auto& header_to_remove : headers_to_remove) {
      // The headers that were originally present in the request have now been removed.
      EXPECT_TRUE(
          upstream_request_->headers().get(Http::LowerCaseString{header_to_remove.first}).empty());
    }

    ASSERT_TRUE(response_->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_body_.length(), upstream_request_->bodyLength());

    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(expected_response_code, response_->headers().getStatusValue());
    EXPECT_EQ(response_size_, response_->body().size());
  }

  void sendExtAuthzResponse(const Headers& headers_to_add, const Headers& headers_to_append,
                            const Headers& headers_to_remove,
                            const Http::TestRequestHeaderMapImpl& new_headers_from_upstream,
                            const Http::TestRequestHeaderMapImpl& headers_to_append_multiple,
                            const Headers& response_headers_to_append,
                            const Headers& response_headers_to_set = {}) {
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

    for (const auto& header_to_remove : headers_to_remove) {
      auto* entry = check_response.mutable_ok_response()->mutable_headers_to_remove();
      entry->Add(std::string(header_to_remove.first));
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

    for (const auto& response_header_to_add : response_headers_to_append) {
      auto* entry = check_response.mutable_ok_response()->mutable_response_headers_to_add()->Add();
      const auto key = std::string(response_header_to_add.first);
      const auto value = std::string(response_header_to_add.second);

      entry->mutable_append()->set_value(true);
      entry->mutable_header()->set_key(key);
      entry->mutable_header()->set_value(value);
      ENVOY_LOG_MISC(trace, "sendExtAuthzResponse: set response_header_to_add {}={}", key, value);
    }

    for (const auto& response_header_to_set : response_headers_to_set) {
      auto* entry = check_response.mutable_ok_response()->mutable_response_headers_to_add()->Add();
      const auto key = std::string(response_header_to_set.first);
      const auto value = std::string(response_header_to_set.second);

      // Replaces the one sent by the upstream.
      entry->mutable_append()->set_value(false);
      entry->mutable_header()->set_key(key);
      entry->mutable_header()->set_value(value);
      ENVOY_LOG_MISC(trace, "sendExtAuthzResponse: set response_header_to_set {}={}", key, value);
    }

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
      AssertionResult result = fake_ext_authz_connection_->close();
      RELEASE_ASSERT(result, result.message());
    }
    cleanupUpstreamAndDownstream();
  }

  const std::string
  expectedCheckRequest(Http::CodecType downstream_protocol,
                       absl::optional<uint64_t> override_expected_size = absl::nullopt) {
    const std::string expected_downstream_protocol =
        downstream_protocol == Http::CodecType::HTTP1 ? "HTTP/1.1" : "HTTP/2";
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

    uint64_t expected_size = override_expected_size.has_value() ? override_expected_size.value()
                                                                : request_body_.length();
    return absl::StrFormat(expected_format, expected_size, expectedRequestBody(),
                           expected_downstream_protocol);
  }

  void expectCheckRequestWithBody(Http::CodecType downstream_protocol, uint64_t request_size) {
    expectCheckRequestWithBodyWithHeaders(downstream_protocol, request_size, Headers{}, Headers{},
                                          Headers{}, Http::TestRequestHeaderMapImpl{},
                                          Http::TestRequestHeaderMapImpl{});
  }

  void expectCheckRequestWithBodyWithHeaders(
      Http::CodecType downstream_protocol, uint64_t request_size, const Headers& headers_to_add,
      const Headers& headers_to_append, const Headers& headers_to_remove,
      const Http::TestRequestHeaderMapImpl& new_headers_from_upstream,
      const Http::TestRequestHeaderMapImpl& headers_to_append_multiple) {
    initializeConfig();
    setDownstreamProtocol(downstream_protocol);
    HttpIntegrationTest::initialize();
    initiateClientConnection(request_size, headers_to_add, headers_to_append, headers_to_remove);
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
    sendExtAuthzResponse(updated_headers_to_add, updated_headers_to_append, headers_to_remove,
                         new_headers_from_upstream, headers_to_append_multiple, Headers{});

    waitForSuccessfulUpstreamResponse("200", updated_headers_to_add, updated_headers_to_append,
                                      headers_to_remove, new_headers_from_upstream,
                                      headers_to_append_multiple);

    cleanup();
  }

  void expectFilterDisableCheck(bool deny_at_disable, bool disable_with_metadata,
                                const std::string& expected_status) {
    initializeConfig(disable_with_metadata);
    setDenyAtDisableRuntimeConfig(deny_at_disable, disable_with_metadata);
    setDownstreamProtocol(Http::CodecType::HTTP2);
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
    allowed_headers:
      patterns:
      - exact: X-Case-Sensitive-Header
      - exact: x-duplicate
      - exact: x-bypass
      - prefix: allowed-prefix
      - safe_regex:
          regex: regex-foo.?

    with_request_body:
      max_request_bytes: 1024
      allow_partial_message: true
  )EOF";
};

class ExtAuthzHttpIntegrationTest : public HttpIntegrationTest,
                                    public TestWithParam<Network::Address::IpVersion> {
public:
  ExtAuthzHttpIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP1);
  }

  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    const auto headers =
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"x-case-sensitive-header", case_sensitive_header_value_},
                                       {"baz", "foo"},
                                       {"bat", "foo"},
                                       {"remove-me", "upstream-should-not-see-me"},
                                       {"x-duplicate", "one"},
                                       {"x-duplicate", "two"},
                                       {"x-duplicate", "three"},
                                       {"allowed-prefix-one", "one"},
                                       {"allowed-prefix-two", "two"},
                                       {"not-allowed", "nope"},
                                       {"authorization", "legit"},
                                       {"regex-food", "food"},
                                       {"regex-fool", "fool"},
                                       {"x-forwarded-for", "1.2.3.4"}};
    if (client_request_body_.empty()) {
      response_ = codec_client_->makeHeaderOnlyRequest(headers);
    } else {
      response_ = codec_client_->makeRequestWithBody(headers, client_request_body_, true);
    }
  }

  void waitForExtAuthzRequest() {
    AssertionResult result =
        fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, fake_ext_authz_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request_);
    RELEASE_ASSERT(result, result.message());
    result = ext_authz_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    // Duplicate headers in the check request should be merged.
    const auto duplicate =
        ext_authz_request_->headers().get(Http::LowerCaseString(std::string("x-duplicate")));
    EXPECT_EQ(1, duplicate.size());
    EXPECT_EQ("one,two,three", duplicate[0]->value().getStringView());
    EXPECT_EQ("one", ext_authz_request_->headers()
                         .get(Http::LowerCaseString(std::string("allowed-prefix-one")))[0]
                         ->value()
                         .getStringView());
    EXPECT_EQ("two", ext_authz_request_->headers()
                         .get(Http::LowerCaseString(std::string("allowed-prefix-two")))[0]
                         ->value()
                         .getStringView());
    EXPECT_EQ("legit", ext_authz_request_->headers()
                           .get(Http::LowerCaseString(std::string("authorization")))[0]
                           ->value()
                           .getStringView());
    EXPECT_TRUE(ext_authz_request_->headers()
                    .get(Http::LowerCaseString(std::string("not-allowed")))
                    .empty());
    EXPECT_EQ("food", ext_authz_request_->headers()
                          .get(Http::LowerCaseString(std::string("regex-food")))[0]
                          ->value()
                          .getStringView());
    EXPECT_EQ("fool", ext_authz_request_->headers()
                          .get(Http::LowerCaseString(std::string("regex-fool")))[0]
                          ->value()
                          .getStringView());

    // Send back authorization response with "baz" and "bat" headers.
    // Also add multiple values "append-foo" and "append-bar" for key "x-append-bat".
    // Also tell Envoy to remove "remove-me" header before sending to upstream.
    Http::TestResponseHeaderMapImpl response_headers{
        {":status", "200"},
        {"baz", "baz"},
        {"bat", "bar"},
        {"x-append-bat", "append-foo"},
        {"x-append-bat", "append-bar"},
        {"x-envoy-auth-headers-to-remove", "remove-me"},
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

  void initializeConfig(bool legacy_allowed_headers = true) {
    config_helper_.addConfigModifier([this, legacy_allowed_headers](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz");

      if (legacy_allowed_headers) {
        TestUtility::loadFromYaml(legacy_default_config_, proto_config_);
      } else {
        TestUtility::loadFromYaml(default_config_, proto_config_);
      }
      envoy::config::listener::v3::Filter ext_authz_filter;
      ext_authz_filter.set_name("envoy.filters.http.ext_authz");
      ext_authz_filter.mutable_typed_config()->PackFrom(proto_config_);

      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_authz_filter));
    });
  }

  void setup(bool legacy_allowed_headers = true) {
    initializeConfig(legacy_allowed_headers);

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

    // The "remove-me" header that was present in the downstream request has
    // been removed by envoy as a result of being present in
    // "x-envoy-auth-headers-to-remove".
    EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString{"remove-me"}).empty());
    // "x-envoy-auth-headers-to-remove" itself has also been removed because
    // it's only used for communication between the authorization server and
    // envoy itself.
    EXPECT_TRUE(upstream_request_->headers()
                    .get(Http::LowerCaseString{"x-envoy-auth-headers-to-remove"})
                    .empty());

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ("200", response_->headers().getStatusValue());

    cleanup();
  }

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config_{};
  FakeHttpConnectionPtr fake_ext_authz_connection_;
  FakeStreamPtr ext_authz_request_;
  IntegrationStreamDecoderPtr response_;
  std::string client_request_body_;
  const Http::LowerCaseString case_sensitive_header_name_{"x-case-sensitive-header"};
  const std::string case_sensitive_header_value_{"Case-Sensitive"};
  const std::string legacy_default_config_ = R"EOF(
  transport_api_version: V3
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 300s

    authorization_request:
      allowed_headers:
        patterns:
        - exact: X-Case-Sensitive-Header
        - exact: x-duplicate
        - prefix: allowed-prefix
        - safe_regex:
            regex: regex-foo.?

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
  const std::string default_config_ = R"EOF(
  transport_api_version: V3
  allowed_headers:
    patterns:
    - exact: X-Case-Sensitive-Header
    - exact: x-duplicate
    - prefix: allowed-prefix
    - safe_regex:
        regex: regex-foo.?
    - exact: x-forwarded-for

  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 300s
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
  with_request_body:
    max_request_bytes: 1024
    allow_partial_message: true
  )EOF";
};

INSTANTIATE_TEST_SUITE_P(IpVersionsCientType, ExtAuthzGrpcIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/1.1.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP1DownstreamRequestWithBody) {
  expectCheckRequestWithBody(Http::CodecType::HTTP1, 4);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/1.1 and the size of the request body is larger than max_request_bytes.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP1DownstreamRequestWithLargeBody) {
  expectCheckRequestWithBody(Http::CodecType::HTTP1, 2048);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/2.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP2DownstreamRequestWithBody) {
  expectCheckRequestWithBody(Http::CodecType::HTTP2, 4);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/2 and the size of the request body is larger than max_request_bytes.
TEST_P(ExtAuthzGrpcIntegrationTest, HTTP2DownstreamRequestWithLargeBody) {
  expectCheckRequestWithBody(Http::CodecType::HTTP2, 2048);
}

// Verifies that the original request headers will be added and appended when the authorization
// server returns headers_to_add, response_headers_to_add, and headers_to_append in OkResponse
// message.
TEST_P(ExtAuthzGrpcIntegrationTest, SendHeadersToAddAndToAppendToUpstream) {
  expectCheckRequestWithBodyWithHeaders(
      Http::CodecType::HTTP1, 4,
      /*headers_to_add=*/Headers{{"header1", "header1"}},
      /*headers_to_append=*/Headers{{"header2", "header2"}},
      /*headers_to_remove=*/Headers{{"remove-me", "upstream-should-not-see-me"}},
      /*new_headers_from_upstream=*/Http::TestRequestHeaderMapImpl{{"new1", "new1"}},
      /*headers_to_append_multiple=*/
      Http::TestRequestHeaderMapImpl{{"multiple", "multiple-first"},
                                     {"multiple", "multiple-second"}});
}

TEST_P(ExtAuthzGrpcIntegrationTest, AllowAtDisable) {
  expectFilterDisableCheck(/*deny_at_disable=*/false, /*disable_with_metadata=*/false, "200");
}

TEST_P(ExtAuthzGrpcIntegrationTest, AllowAtDisableWithMetadata) {
  expectFilterDisableCheck(/*deny_at_disable=*/false, /*disable_with_metadata=*/true, "200");
}

TEST_P(ExtAuthzGrpcIntegrationTest, DenyAtDisable) {
  expectFilterDisableCheck(/*deny_at_disable=*/true, /*disable_with_metadata=*/false, "403");
}

TEST_P(ExtAuthzGrpcIntegrationTest, DenyAtDisableWithMetadata) {
  expectFilterDisableCheck(/*deny_at_disable=*/true, /*disable_with_metadata=*/true, "403");
}

TEST_P(ExtAuthzGrpcIntegrationTest, CheckAfterBufferingComplete) {
  // Set up ext_authz filter.
  initializeConfig();

  // Use h1, set up the test.
  setDownstreamProtocol(Http::CodecType::HTTP1);
  HttpIntegrationTest::initialize();

  // Start a client connection and start request.
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"},           {":path", "/test"},
      {":scheme", "http"},           {":authority", "host"},
      {"x-duplicate", "one"},        {"x-duplicate", "two"},
      {"x-duplicate", "three"},      {"allowed-prefix-one", "one"},
      {"allowed-prefix-two", "two"}, {"not-allowed", "nope"},
      {"regex-food", "food"},        {"regex-fool", "fool"}};

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));

  auto encoder_decoder = codec_client_->startRequest(headers);
  response_ = std::move(encoder_decoder.second);

  // Split the request body into two parts
  TestUtility::feedBufferWithRandomCharacters(request_body_, 10000);
  std::string initial_body = request_body_.toString().substr(0, 2000);
  std::string final_body = request_body_.toString().substr(2000, 8000);

  // Send the first part of the request body without ending the stream
  codec_client_->sendData(encoder_decoder.first, initial_body, false);

  // Expect that since the buffer is full, the request is sent to the authorization server
  waitForExtAuthzRequest(expectedCheckRequest(Http::CodecType::HTTP1, 2000));

  sendExtAuthzResponse(Headers{}, Headers{}, Headers{}, Http::TestRequestHeaderMapImpl{},
                       Http::TestRequestHeaderMapImpl{}, Headers{}, Headers{});

  // Send the rest of the data and end the stream
  codec_client_->sendData(encoder_decoder.first, final_body, true);

  // Expect a 200 OK response
  waitForSuccessfulUpstreamResponse("200");

  const std::string expected_body(response_size_, 'a');
  verifyResponse(std::move(response_), "200",
                 Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                 {"replaceable", "set-by-upstream"},
                                                 {"set-cookie", "cookie1=snickerdoodle"}},
                 expected_body);

  cleanup();
}

TEST_P(ExtAuthzGrpcIntegrationTest, DownstreamHeadersOnSuccess) {
  // Set up ext_authz filter.
  initializeConfig();

  // Use h1, set up the test.
  setDownstreamProtocol(Http::CodecType::HTTP1);
  HttpIntegrationTest::initialize();

  // Start a client connection and request.
  initiateClientConnection(0);

  // Wait for the ext_authz request as a result of the client request.
  waitForExtAuthzRequest(expectedCheckRequest(Http::CodecType::HTTP1));

  // Send back an ext_authz response with response_headers_to_add set.
  sendExtAuthzResponse(
      Headers{}, Headers{}, Headers{}, Http::TestRequestHeaderMapImpl{},
      Http::TestRequestHeaderMapImpl{},
      Headers{{"downstream2", "downstream-should-see-me"}, {"set-cookie", "cookie2=gingerbread"}},
      Headers{{"replaceable", "by-ext-authz"}});

  // Wait for the upstream response.
  waitForSuccessfulUpstreamResponse("200");

  EXPECT_EQ(Http::HeaderUtility::getAllOfHeaderAsString(response_->headers(),
                                                        Http::LowerCaseString("set-cookie"))
                .result()
                .value(),
            "cookie1=snickerdoodle,cookie2=gingerbread");

  // Verify the response is HTTP 200 with the header from `response_headers_to_add` above.
  const std::string expected_body(response_size_, 'a');
  verifyResponse(std::move(response_), "200",
                 Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                 {"downstream2", "downstream-should-see-me"},
                                                 {"replaceable", "by-ext-authz"}},
                 expected_body);
  cleanup();
}

TEST_P(ExtAuthzGrpcIntegrationTest, FailureModeAllowNonUtf8) {
  // Set up ext_authz filter.
  initializeConfig(false, true);

  // Use h1, set up the test.
  setDownstreamProtocol(Http::CodecType::HTTP1);
  HttpIntegrationTest::initialize();

  // Start a client connection and request.
  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));

  std::string invalid_unicode("valid_prefix");
  invalid_unicode.append(1, char(0xc3));
  invalid_unicode.append(1, char(0x28));
  invalid_unicode.append("valid_suffix");
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"},           {":path", "/test"},
      {":scheme", "http"},           {":authority", "host"},
      {"x-bypass", invalid_unicode}, {"allowed-prefix-one", "one"},
      {"allowed-prefix-two", "two"}, {"not-allowed", "nope"},
      {"x-duplicate", "one"},        {"x-duplicate", "two"},
      {"x-duplicate", "three"},      {"regex-food", "food"},
      {"regex-fool", "fool"}};

  response_ = codec_client_->makeRequestWithBody(headers, {});

  // Wait for the ext_authz request as a result of the client request.
  waitForExtAuthzRequest(expectedCheckRequest(Http::CodecType::HTTP1));

  // Send back an ext_authz response with response_headers_to_add set.
  sendExtAuthzResponse(
      Headers{}, Headers{}, Headers{}, Http::TestRequestHeaderMapImpl{},
      Http::TestRequestHeaderMapImpl{},
      Headers{{"downstream2", "downstream-should-see-me"}, {"set-cookie", "cookie2=gingerbread"}},
      Headers{{"replaceable", "by-ext-authz"}});

  // Wait for the upstream response.
  waitForSuccessfulUpstreamResponse("200");

  EXPECT_EQ(Http::HeaderUtility::getAllOfHeaderAsString(response_->headers(),
                                                        Http::LowerCaseString("set-cookie"))
                .result()
                .value(),
            "cookie1=snickerdoodle,cookie2=gingerbread");

  // Verify the response is HTTP 200 with the header from `response_headers_to_add` above.
  const std::string expected_body(response_size_, 'a');
  verifyResponse(std::move(response_), "200",
                 Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                 {"downstream2", "downstream-should-see-me"},
                                                 {"replaceable", "by-ext-authz"}},
                 expected_body);
  cleanup();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ExtAuthzHttpIntegrationTest,
                         ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that by default HTTP service uses the case-sensitive string matcher
// (uses legacy config for allowed_headers).
TEST_P(ExtAuthzHttpIntegrationTest,
       DEPRECATED_FEATURE_TEST(LegacyDefaultCaseSensitiveStringMatcher)) {
  setup();
  const auto header_entry = ext_authz_request_->headers().get(case_sensitive_header_name_);
  ASSERT_TRUE(header_entry.empty());
}

// (uses legacy config for allowed_headers).
TEST_P(ExtAuthzHttpIntegrationTest, DEPRECATED_FEATURE_TEST(LegacyDirectReponse)) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_hosts = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        virtual_hosts->mutable_routes(0)->clear_route();
        envoy::config::route::v3::Route* route = virtual_hosts->mutable_routes(0);
        route->mutable_direct_response()->set_status(204);
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  initiateClientConnection();
  waitForExtAuthzRequest();

  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ("204", response_->headers().Status()->value().getStringView());
}

// (uses legacy config for allowed_headers).
TEST_P(ExtAuthzHttpIntegrationTest, DEPRECATED_FEATURE_TEST(LegacyRedirectResponse)) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_hosts = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        virtual_hosts->mutable_routes(0)->clear_route();
        envoy::config::route::v3::Route* route = virtual_hosts->mutable_routes(0);
        route->mutable_redirect()->set_path_redirect("/redirect");
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  initiateClientConnection();
  waitForExtAuthzRequest();

  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ("301", response_->headers().Status()->value().getStringView());
  EXPECT_EQ("http://host/redirect", response_->headers().getLocationValue());
}

// Verifies that by default HTTP service uses the case-sensitive string matcher
// (uses new config for allowed_headers).
TEST_P(ExtAuthzHttpIntegrationTest, DefaultCaseSensitiveStringMatcher) {
  setup(false);
  const auto header_entry = ext_authz_request_->headers().get(case_sensitive_header_name_);
  ASSERT_TRUE(header_entry.empty());
}

// Verifies that "X-Forwarded-For" header is unmodified.
TEST_P(ExtAuthzHttpIntegrationTest, UnmodifiedForwardedForHeader) {
  setup(false);
  EXPECT_THAT(ext_authz_request_->headers(), Http::HeaderValueOf("x-forwarded-for", "1.2.3.4"));
}

// Verifies that local address is appended to "X-Forwarded-For" header
// if "envoy.reloadable_features.ext_authz_http_send_original_xff" runtime guard is disabled.
TEST_P(ExtAuthzHttpIntegrationTest, LegacyAppendLocalAddressToForwardedForHeader) {
  TestScopedRuntime scoped_runtime_;
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.ext_authz_http_send_original_xff", "false"}});

  setup(false);

  const auto local_address = test_server_->server().localInfo().address()->ip()->addressAsString();
  EXPECT_THAT(ext_authz_request_->headers(),
              Http::HeaderValueOf("x-forwarded-for", absl::StrCat("1.2.3.4", ",", local_address)));
}

// Verifies that by default HTTP service uses the case-sensitive string matcher
// (uses new config for allowed_headers).
TEST_P(ExtAuthzHttpIntegrationTest, Body) {
  client_request_body_ = "the request body";
  setup(false);
  EXPECT_EQ(ext_authz_request_->body().toString(), client_request_body_);
}

TEST_P(ExtAuthzHttpIntegrationTest, BodyNonUtf8) {
  client_request_body_ = "valid_prefix";
  client_request_body_.append(1, char(0xc3));
  client_request_body_.append(1, char(0x28));
  client_request_body_.append("valid_suffix");
  setup(false);
  EXPECT_EQ(ext_authz_request_->body().toString(), client_request_body_);
}

// (uses new config for allowed_headers).
TEST_P(ExtAuthzHttpIntegrationTest, DirectReponse) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_hosts = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        virtual_hosts->mutable_routes(0)->clear_route();
        envoy::config::route::v3::Route* route = virtual_hosts->mutable_routes(0);
        route->mutable_direct_response()->set_status(204);
      });

  initializeConfig(false);
  HttpIntegrationTest::initialize();
  initiateClientConnection();
  waitForExtAuthzRequest();

  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ("204", response_->headers().Status()->value().getStringView());
}

// (uses new config for allowed_headers).
TEST_P(ExtAuthzHttpIntegrationTest, RedirectResponse) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* virtual_hosts = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        virtual_hosts->mutable_routes(0)->clear_route();
        envoy::config::route::v3::Route* route = virtual_hosts->mutable_routes(0);
        route->mutable_redirect()->set_path_redirect("/redirect");
      });

  initializeConfig(false);
  HttpIntegrationTest::initialize();
  initiateClientConnection();
  waitForExtAuthzRequest();

  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ("301", response_->headers().Status()->value().getStringView());
  EXPECT_EQ("http://host/redirect", response_->headers().getLocationValue());
}

class ExtAuthzLocalReplyIntegrationTest : public HttpIntegrationTest,
                                          public TestWithParam<Network::Address::IpVersion> {
public:
  ExtAuthzLocalReplyIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP1);
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

  FakeHttpConnectionPtr fake_ext_authz_connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ExtAuthzLocalReplyIntegrationTest,
                         ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// This integration test uses ext_authz combined with `local_reply_config`.
// * If ext_authz response status is 401; its response headers and body are sent to the client.
// * But if `local_reply_config` is specified, the response body and its content-length and type
//   are controlled by the `local_reply_config`.
// This integration test verifies that content-type and content-length generated
// from `local_reply_config` are not overridden by ext_authz response.
TEST_P(ExtAuthzLocalReplyIntegrationTest, DeniedHeaderTest) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
    ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    ext_authz_cluster->set_name("ext_authz");

    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config;
    const std::string ext_authz_config = R"EOF(
  transport_api_version: V3
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 300s
  )EOF";
    TestUtility::loadFromYaml(ext_authz_config, proto_config);

    envoy::config::listener::v3::Filter ext_authz_filter;
    ext_authz_filter.set_name("envoy.filters.http.ext_authz");
    ext_authz_filter.mutable_typed_config()->PackFrom(proto_config);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_authz_filter));
  });

  const std::string local_reply_yaml = R"EOF(
body_format:
  json_format:
    code: "%RESPONSE_CODE%"
    message: "%LOCAL_REPLY_BODY%"
  )EOF";
  envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig
      local_reply_config;
  TestUtility::loadFromYaml(local_reply_yaml, local_reply_config);
  config_helper_.setLocalReply(local_reply_config);

  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  AssertionResult result =
      fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, fake_ext_authz_connection_);
  RELEASE_ASSERT(result, result.message());
  FakeStreamPtr ext_authz_request;
  result = fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request);
  RELEASE_ASSERT(result, result.message());
  result = ext_authz_request->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  Http::TestResponseHeaderMapImpl ext_authz_response_headers{
      {":status", "401"},
      {"content-type", "fake-type"},
  };
  ext_authz_request->encodeHeaders(ext_authz_response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("401", response->headers().Status()->value().getStringView());
  // Without fixing the bug, "content-type" and "content-length" are overridden by the ext_authz
  // responses as its "content-type: fake-type" and "content-length: 0".
  EXPECT_EQ("application/json", response->headers().ContentType()->value().getStringView());
  EXPECT_EQ("26", response->headers().ContentLength()->value().getStringView());

  const std::string expected_body = R"({
      "code": 401,
      "message": ""
})";
  EXPECT_TRUE(TestUtility::jsonStringEqual(response->body(), expected_body));

  cleanup();
}

TEST_P(ExtAuthzGrpcIntegrationTest, GoogleAsyncClientCreation) {
  initializeConfig();
  setDownstreamProtocol(Http::CodecType::HTTP2);
  HttpIntegrationTest::initialize();

  int expected_grpc_client_creation_count = 0;

  initiateClientConnection(4, Headers{}, Headers{});
  waitForExtAuthzRequest(expectedCheckRequest(Http::CodecClient::Type::HTTP2));
  sendExtAuthzResponse(Headers{}, Headers{}, Headers{}, Http::TestRequestHeaderMapImpl{},
                       Http::TestRequestHeaderMapImpl{}, Headers{});

  if (clientType() == Grpc::ClientType::GoogleGrpc) {

    // Make sure Google grpc client is created before the request coming in.
    // Since this is not laziness creation, it should create one client per
    // thread before the traffic comes.
    expected_grpc_client_creation_count =
        test_server_->counter("grpc.ext_authz_cluster.google_grpc_client_creation")->value();
  }

  waitForSuccessfulUpstreamResponse("200");

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};
  TestUtility::feedBufferWithRandomCharacters(request_body_, 4);
  response_ = codec_client_->makeRequestWithBody(headers, request_body_.toString());

  auto result = fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request_);
  RELEASE_ASSERT(result, result.message());

  envoy::service::auth::v3::CheckRequest check_request;
  result = ext_authz_request_->waitForGrpcMessage(*dispatcher_, check_request);
  RELEASE_ASSERT(result, result.message());

  EXPECT_EQ("POST", ext_authz_request_->headers().getMethodValue());
  EXPECT_EQ("/envoy.service.auth.v3.Authorization/Check",
            ext_authz_request_->headers().getPathValue());
  EXPECT_EQ("application/grpc", ext_authz_request_->headers().getContentTypeValue());
  result = ext_authz_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  if (clientType() == Grpc::ClientType::GoogleGrpc) {
    // Make sure no more Google grpc client is created no matter how many requests coming in.
    EXPECT_EQ(expected_grpc_client_creation_count,
              test_server_->counter("grpc.ext_authz_cluster.google_grpc_client_creation")->value());
  }
  sendExtAuthzResponse(Headers{}, Headers{}, Headers{}, Http::TestRequestHeaderMapImpl{},
                       Http::TestRequestHeaderMapImpl{}, Headers{});

  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  result = upstream_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(response_size_, true);

  ASSERT_TRUE(response_->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_body_.length(), upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_EQ("200", response_->headers().getStatusValue());
  EXPECT_EQ(response_size_, response_->body().size());

  if (clientType() == Grpc::ClientType::GoogleGrpc) {
    // Make sure no more Google grpc client is created no matter how many requests coming in.
    EXPECT_EQ(expected_grpc_client_creation_count,
              test_server_->counter("grpc.ext_authz_cluster.google_grpc_client_creation")->value());
  }

  cleanup();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/17344
TEST(ExtConfigValidateTest, Validate) {
  Server::TestComponentFactory component_factory;
  EXPECT_TRUE(validateConfig(testing::NiceMock<Server::MockOptions>(TestEnvironment::runfilesPath(
                                 "test/extensions/filters/http/ext_authz/ext_authz.yaml")),
                             Network::Address::InstanceConstSharedPtr(), component_factory,
                             Thread::threadFactoryForTest(), Filesystem::fileSystemForTest()));
}

} // namespace Envoy
