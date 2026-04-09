#include <string>
#include <vector>

#include "envoy/extensions/filters/http/a2a/v3/a2a.pb.h"

#include "test/integration/fake_access_log.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {
namespace {

class A2aFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  A2aFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void initializeFilter(const std::string& config = "") {
    const std::string filter_config = config.empty() ? R"EOF(
      name: envoy.filters.http.a2a
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.a2a.v3.A2a
        traffic_mode: PASS_THROUGH
    )EOF"
                                                     : config;

    config_helper_.prependFilter(filter_config);
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, A2aFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test that a non-POST request is ignored and passes through (PASS_THROUGH mode).
TEST_P(A2aFilterIntegrationTest, NonPostRequestIgnored) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that a valid A2A JSON-RPC POST request passes through successfully.
TEST_P(A2aFilterIntegrationTest, ValidA2aPostRequest) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string request_body = R"({"jsonrpc": "2.0", "method": "test", "id": "1"})";
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_body, upstream_request_->body().toString());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that a valid A2A JSON-RPC POST request with rich metadata fields sets dynamic metadata.
TEST_P(A2aFilterIntegrationTest, ValidA2aPostRequestWithRichMetadata) {
  FakeAccessLogFactory factory;
  Registry::InjectFactory<AccessLog::AccessLogInstanceFactory> factory_register(factory);

  bool metadata_verified = false;
  factory.setLogCallback(
      [&metadata_verified](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
        const auto& dynamic_metadata = stream_info.dynamicMetadata().filter_metadata();
        auto it = dynamic_metadata.find("envoy.filters.http.a2a");
        if (it != dynamic_metadata.end()) {
          const auto& fields = it->second.fields();

          auto it_jsonrpc = fields.find("jsonrpc");
          ASSERT_NE(it_jsonrpc, fields.end());
          EXPECT_EQ("2.0", it_jsonrpc->second.string_value());

          auto it_method = fields.find("method");
          ASSERT_NE(it_method, fields.end());
          EXPECT_EQ("message/send", it_method->second.string_value());

          auto it_id = fields.find("id");
          ASSERT_NE(it_id, fields.end());
          EXPECT_EQ("123", it_id->second.string_value());

          auto it_params = fields.find("params");
          ASSERT_NE(it_params, fields.end());
          const auto& params = it_params->second.struct_value().fields();
          auto it_taskId = params.find("taskId");
          ASSERT_NE(it_taskId, params.end());
          EXPECT_EQ("task-abc", it_taskId->second.string_value());

          auto it_msg = params.find("message");
          ASSERT_NE(it_msg, params.end());
          const auto& msg = it_msg->second.struct_value().fields();
          auto it_msg_taskId = msg.find("taskId");
          ASSERT_NE(it_msg_taskId, msg.end());
          EXPECT_EQ("msg-task-123", it_msg_taskId->second.string_value());
          metadata_verified = true;
        }
      });

  config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
    auto* access_log = hcm.add_access_log();
    access_log->set_name("envoy.access_loggers.test");
    test::integration::accesslog::FakeAccessLog access_log_config;
    access_log->mutable_typed_config()->PackFrom(access_log_config);
  });

  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string request_body = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "123",
    "params": {
      "taskId": "task-abc",
      "message": {
        "taskId": "msg-task-123",
        "parts": ["part1", "part2"]
      }
    }
  })";
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_body, upstream_request_->body().toString());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_TRUE(metadata_verified);
}

// Test that a valid A2A JSON-RPC POST request with large body passes through successfully.
TEST_P(A2aFilterIntegrationTest, ValidLargeA2aPostRequest) {
  // Configure filter with a larger limit (e.g. 1MB) to allow the large payload
  initializeFilter(R"EOF(
    name: envoy.filters.http.a2a
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.a2a.v3.A2a
      traffic_mode: PASS_THROUGH
      max_request_body_size: { value: 1048576 }
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Create a body around 64KB
  std::string padding(1024 * 64, 'a');
  const std::string request_body =
      fmt::format(R"({{"jsonrpc": "2.0", "method": "test", "params": "{}"}})", padding);

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_body, upstream_request_->body().toString());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that with default configuration (8KB limit), a 16KB body is rejected.
TEST_P(A2aFilterIntegrationTest, BodyTooLargeDefaultLimit) {
  initializeFilter(); // Default config (8KB limit)

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Create a body around 16KB, which exceeds the default 8KB limit.
  std::string padding(1024 * 16, 'a');
  const std::string request_body =
      fmt::format(R"({{"jsonrpc": "2.0", "method": "test", "params": "{}"}})", padding);

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  // Verify it was rejected due to body size.
  EXPECT_THAT(response->body(), testing::HasSubstr("request body is too large."));
}

// Test that an A2A request with malformed JSON is rejected with a 400.
TEST_P(A2aFilterIntegrationTest, InvalidJsonBodyRejected) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"jsonrpc": "2.0",)"); // Malformed JSON

  ASSERT_TRUE(response->waitForEndStream());
  // The upstream should NOT receive a request because the filter sends a local reply.
  EXPECT_FALSE(upstream_request_ != nullptr);
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test that an A2A request missing the jsonrpc field is rejected (if strict validation is assumed).
// Assuming the filter enforces JSON-RPC 2.0 structure for requests identified as A2A.
TEST_P(A2aFilterIntegrationTest, MissingJsonRpcFieldRejected) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.a2a
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.a2a.v3.A2a
      traffic_mode: REJECT
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"method": "test"})");

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_ != nullptr);
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test that a POST request with the wrong content type is ignored and passes through (PASS_THROUGH
// mode).
TEST_P(A2aFilterIntegrationTest, WrongContentTypePostRequestIgnored) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string request_body = R"({"jsonrpc": "2.0", "method": "test"})";
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "text/plain"}}, // Incorrect content type
      request_body);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test REJECT mode - non-A2A traffic rejected (wrong content type).
TEST_P(A2aFilterIntegrationTest, RejectModeRejectsNonA2aContentType) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.a2a
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.a2a.v3.A2a
      traffic_mode: REJECT
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "text/plain"}},
      R"({"method": "test"})");

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test REJECT mode rejects GET requests with body
TEST_P(A2aFilterIntegrationTest, RejectModeRejectsGetRequestWithBody) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.a2a
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.a2a.v3.A2a
      traffic_mode: REJECT
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string request_body = "this body should be ignored by filter";
  // According to RFC 7231, a payload body in a GET request has no defined semantics.
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}},
      request_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_ != nullptr);
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test Max Request Body Size limit.
TEST_P(A2aFilterIntegrationTest, BodyTooLargeRejected) {
  initializeFilter(R"EOF(
    name: envoy.filters.http.a2a
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.a2a.v3.A2a
      traffic_mode: PASS_THROUGH
      max_request_body_size: { value: 10 }
  )EOF");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string large_body =
      R"({"jsonrpc": "2.0", "method": "very_long_method_name"})"; // > 10 bytes
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      large_body);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("request body is too large"));
}

// Test that invalid JSON syntax triggers an immediate error during parsing (before end of stream).
TEST_P(A2aFilterIntegrationTest, ImmediateInvalidJsonRejected) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Sending invalid JSON syntax:
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "invalid_json_content");

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_FALSE(upstream_request_);
  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("not a valid JSON"));
  EXPECT_THAT(response->body(), testing::Not(testing::HasSubstr("(incomplete)")));
}

// Test that a chunked A2A request is buffered and successfully parsed.
TEST_P(A2aFilterIntegrationTest, ChunkedValidA2aPostRequest) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}});
  auto& request_encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  const std::string part1 = R"({"jsonrpc": "2.0", "method": )";
  const std::string part2 = R"("test", "id": "1"})";

  codec_client_->sendData(request_encoder, part1, false);
  codec_client_->sendData(request_encoder, part2, true);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(part1 + part2, upstream_request_->body().toString());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
