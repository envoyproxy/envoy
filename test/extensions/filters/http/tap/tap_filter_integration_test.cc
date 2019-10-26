#include <fstream>

#include "envoy/data/tap/v2alpha/wrapper.pb.h"

#include "test/integration/http_integration.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TapIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                           public HttpIntegrationTest {
public:
  TapIntegrationTest()
      // Note: This test must use HTTP/2 because of the lack of early close detection for
      // HTTP/1 on OSX. In this test we close the admin /tap stream when we don't want any
      // more data, and without immediate close detection we can't have a flake free test.
      // Thus, we use HTTP/2 for everything here.
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {

    // Also use HTTP/2 for upstream so that we can fully test trailers.
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);
    initialize();
  }

  const envoy::api::v2::core::HeaderValue*
  findHeader(const std::string& key,
             const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>& headers) {
    for (const auto& header : headers) {
      if (header.key() == key) {
        return &header;
      }
    }

    return nullptr;
  }

  void makeRequest(const Http::TestHeaderMapImpl& request_headers,
                   const std::vector<std::string>& request_body_chunks,
                   const Http::TestHeaderMapImpl* request_trailers,
                   const Http::TestHeaderMapImpl& response_headers,
                   const std::vector<std::string>& response_body_chunks,
                   const Http::TestHeaderMapImpl* response_trailers) {
    IntegrationStreamDecoderPtr decoder;
    if (request_trailers == nullptr && request_body_chunks.empty()) {
      decoder = codec_client_->makeHeaderOnlyRequest(request_headers);
    } else {
      auto result = codec_client_->startRequest(request_headers);
      decoder = std::move(result.second);

      for (uint64_t index = 0; index < request_body_chunks.size(); index++) {
        Buffer::OwnedImpl data(request_body_chunks[index]);
        result.first.encodeData(data, index == request_body_chunks.size() - 1 &&
                                          request_trailers == nullptr);
      }
      if (request_trailers != nullptr) {
        result.first.encodeTrailers(*request_trailers);
      }
    }

    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(response_headers,
                                     response_trailers == nullptr && response_body_chunks.empty());
    for (uint64_t index = 0; index < response_body_chunks.size(); index++) {
      Buffer::OwnedImpl data(response_body_chunks[index]);
      upstream_request_->encodeData(data, index == response_body_chunks.size() - 1 &&
                                              response_trailers == nullptr);
    }
    if (response_trailers != nullptr) {
      upstream_request_->encodeTrailers(*response_trailers);
    }

    decoder->waitForEndStream();
  }

  void startAdminRequest(const std::string& admin_request_yaml) {
    admin_client_ = makeHttpConnection(makeClientConnection(lookupPort("admin")));
    const Http::TestHeaderMapImpl admin_request_headers{
        {":method", "POST"}, {":path", "/tap"}, {":scheme", "http"}, {":authority", "host"}};
    admin_response_ = admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml);
    admin_response_->waitForHeaders();
    EXPECT_EQ("200", admin_response_->headers().Status()->value().getStringView());
    EXPECT_FALSE(admin_response_->complete());
  }

  std::string getTempPathPrefix() {
    const std::string path_prefix = TestEnvironment::temporaryDirectory() + "/tap_integration_" +
                                    testing::UnitTest::GetInstance()->current_test_info()->name() +
                                    "/";
    TestEnvironment::createPath(path_prefix);
    return path_prefix;
  }

  std::vector<envoy::data::tap::v2alpha::TraceWrapper>
  readTracesFromFile(const std::string& path_prefix) {
    // Find the written .pb file and verify it.
    auto files = TestUtility::listFiles(path_prefix, false);
    auto pb_file_name = std::find_if(files.begin(), files.end(), [](const std::string& s) {
      return absl::EndsWith(s, MessageUtil::FileExtensions::get().ProtoBinaryLengthDelimited);
    });
    EXPECT_NE(pb_file_name, files.end());

    std::vector<envoy::data::tap::v2alpha::TraceWrapper> traces;
    std::ifstream pb_file(*pb_file_name);
    Protobuf::io::IstreamInputStream stream(&pb_file);
    Protobuf::io::CodedInputStream coded_stream(&stream);
    while (true) {
      uint32_t message_size;
      if (!coded_stream.ReadVarint32(&message_size)) {
        break;
      }

      traces.emplace_back();

      auto limit = coded_stream.PushLimit(message_size);
      EXPECT_TRUE(traces.back().ParseFromCodedStream(&coded_stream));
      coded_stream.PopLimit(limit);
    }

    return traces;
  }

  const Http::TestHeaderMapImpl request_headers_tap_{{":method", "GET"},
                                                     {":path", "/"},
                                                     {":scheme", "http"},
                                                     {":authority", "host"},
                                                     {"foo", "bar"}};

  const Http::TestHeaderMapImpl request_headers_no_tap_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  const Http::TestHeaderMapImpl request_trailers_{{"foo_trailer", "bar"}};

  const Http::TestHeaderMapImpl response_headers_tap_{{":status", "200"}, {"bar", "baz"}};

  const Http::TestHeaderMapImpl response_headers_no_tap_{{":status", "200"}};

  const Http::TestHeaderMapImpl response_trailers_{{"bar_trailer", "baz"}};

  const std::string admin_filter_config_ =
      R"EOF(
name: envoy.filters.http.tap
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.tap.v2alpha.Tap
  common_config:
    admin_config:
      config_id: test_config_id
)EOF";

  IntegrationCodecClientPtr admin_client_;
  IntegrationStreamDecoderPtr admin_response_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TapIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify a static configuration with an any matcher, writing to a file per tap sink.
TEST_P(TapIntegrationTest, StaticFilePerTap) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.tap
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.tap.v2alpha.Tap
  common_config:
    static_config:
      match_config:
        any_match: true
      output_config:
        sinks:
          - format: PROTO_BINARY
            file_per_tap:
              path_prefix: {}
)EOF";

  const std::string path_prefix = getTempPathPrefix();
  initializeFilter(fmt::format(filter_config, path_prefix));

  // Initial request/response with tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  // Find the written .pb file and verify it.
  auto files = TestUtility::listFiles(path_prefix, false);
  auto pb_file = std::find_if(files.begin(), files.end(),
                              [](const std::string& s) { return absl::EndsWith(s, ".pb"); });
  ASSERT_NE(pb_file, files.end());

  envoy::data::tap::v2alpha::TraceWrapper trace;
  TestUtility::loadFromFile(*pb_file, trace, *api_);
  EXPECT_TRUE(trace.has_http_buffered_trace());
}

// Verify a basic tap flow using the admin handler.
TEST_P(TapIntegrationTest, AdminBasicFlow) {
  initializeFilter(admin_filter_config_);

  // Initial request/response with no tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    or_match:
      rules:
        - http_request_headers_match:
            headers:
              - name: foo
                exact_match: bar
        - http_response_headers_match:
            headers:
              - name: bar
                exact_match: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  // Setup a tap and disconnect it without any request/response.
  startAdminRequest(admin_request_yaml);
  admin_client_->close();
  test_server_->waitForGaugeEq("http.admin.downstream_rq_active", 0);

  // Second request/response with no tap.
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  // Setup the tap again and leave it open.
  startAdminRequest(admin_request_yaml);

  // Do a request which should tap, matching on request headers.
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  // Wait for the tap message.
  admin_response_->waitForBodyData(1);
  envoy::data::tap::v2alpha::TraceWrapper trace;
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_EQ(trace.http_buffered_trace().request().headers().size(), 8);
  EXPECT_EQ(trace.http_buffered_trace().response().headers().size(), 4);
  admin_response_->clearBody();

  // Do a request which should not tap.
  makeRequest(request_headers_no_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  // Do a request which should tap, matching on response headers.
  makeRequest(request_headers_no_tap_, {}, nullptr, response_headers_tap_, {}, nullptr);

  // Wait for the tap message.
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_EQ(trace.http_buffered_trace().request().headers().size(), 7);
  EXPECT_EQ(
      "http",
      findHeader("x-forwarded-proto", trace.http_buffered_trace().request().headers())->value());
  EXPECT_EQ(trace.http_buffered_trace().response().headers().size(), 5);
  EXPECT_NE(nullptr, findHeader("date", trace.http_buffered_trace().response().headers()));
  EXPECT_EQ("baz", findHeader("bar", trace.http_buffered_trace().response().headers())->value());

  admin_client_->close();
  test_server_->waitForGaugeEq("http.admin.downstream_rq_active", 0);

  // Now setup a tap that matches on logical AND.
  const std::string admin_request_yaml2 =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    and_match:
      rules:
        - http_request_headers_match:
            headers:
              - name: foo
                exact_match: bar
        - http_response_headers_match:
            headers:
              - name: bar
                exact_match: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml2);

  // Do a request that matches, but the response does not match. No tap.
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  // Do a request that doesn't match, but the response does match. No tap.
  makeRequest(request_headers_no_tap_, {}, nullptr, response_headers_tap_, {}, nullptr);

  // Do a request that matches and a response that matches. Should tap.
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_tap_, {}, nullptr);

  // Wait for the tap message.
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);

  admin_client_->close();
  EXPECT_EQ(3UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

// Verify both request and response trailer matching works.
TEST_P(TapIntegrationTest, AdminTrailers) {
  initializeFilter(admin_filter_config_);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    and_match:
      rules:
        - http_request_trailers_match:
            headers:
              - name: foo_trailer
                exact_match: bar
        - http_response_trailers_match:
            headers:
              - name: bar_trailer
                exact_match: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {}, &request_trailers_, response_headers_no_tap_, {},
              &response_trailers_);

  envoy::data::tap::v2alpha::TraceWrapper trace;
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_EQ("bar",
            findHeader("foo_trailer", trace.http_buffered_trace().request().trailers())->value());
  EXPECT_EQ("baz",
            findHeader("bar_trailer", trace.http_buffered_trace().response().trailers())->value());

  admin_client_->close();
}

// Verify admin tapping with request/response body as bytes.
TEST_P(TapIntegrationTest, AdminBodyAsBytes) {
  initializeFilter(admin_filter_config_);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    any_match: true
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {{"hello"}}, nullptr, response_headers_no_tap_, {{"world"}},
              nullptr);
  envoy::data::tap::v2alpha::TraceWrapper trace;
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_EQ("hello", trace.http_buffered_trace().request().body().as_bytes());
  EXPECT_FALSE(trace.http_buffered_trace().request().body().truncated());
  EXPECT_EQ("world", trace.http_buffered_trace().response().body().as_bytes());
  EXPECT_FALSE(trace.http_buffered_trace().response().body().truncated());

  admin_client_->close();
}

// Verify admin tapping with request/response body as strings.
TEST_P(TapIntegrationTest, AdminBodyAsString) {
  initializeFilter(admin_filter_config_);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    any_match: true
  output_config:
    sinks:
      - format: JSON_BODY_AS_STRING
        streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {{"hello"}}, nullptr, response_headers_no_tap_, {{"world"}},
              nullptr);
  envoy::data::tap::v2alpha::TraceWrapper trace;
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_EQ("hello", trace.http_buffered_trace().request().body().as_string());
  EXPECT_FALSE(trace.http_buffered_trace().request().body().truncated());
  EXPECT_EQ("world", trace.http_buffered_trace().response().body().as_string());
  EXPECT_FALSE(trace.http_buffered_trace().response().body().truncated());

  admin_client_->close();
}

// Verify admin tapping with truncated request/response body.
TEST_P(TapIntegrationTest, AdminBodyAsBytesTruncated) {
  initializeFilter(admin_filter_config_);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match_config:
    any_match: true
  output_config:
    max_buffered_rx_bytes: 3
    max_buffered_tx_bytes: 4
    sinks:
      - streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {{"hello"}}, nullptr, response_headers_no_tap_, {{"world"}},
              nullptr);
  envoy::data::tap::v2alpha::TraceWrapper trace;
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_EQ("hel", trace.http_buffered_trace().request().body().as_bytes());
  EXPECT_TRUE(trace.http_buffered_trace().request().body().truncated());
  EXPECT_EQ("worl", trace.http_buffered_trace().response().body().as_bytes());
  EXPECT_TRUE(trace.http_buffered_trace().response().body().truncated());

  admin_client_->close();
}

// Verify a static configuration with a request header matcher, writing to a streamed file per tap
// sink.
TEST_P(TapIntegrationTest, StaticFilePerTapStreaming) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.tap
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.tap.v2alpha.Tap
  common_config:
    static_config:
      match_config:
        http_request_headers_match:
          headers:
            - name: foo
              exact_match: bar
      output_config:
        streaming: true
        sinks:
          - format: PROTO_BINARY_LENGTH_DELIMITED
            file_per_tap:
              path_prefix: {}
)EOF";

  const std::string path_prefix = getTempPathPrefix();
  initializeFilter(fmt::format(filter_config, path_prefix));

  // Initial request/response with tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {"hello"}, &request_trailers_, response_headers_no_tap_,
              {"world"}, &response_trailers_);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  std::vector<envoy::data::tap::v2alpha::TraceWrapper> traces = readTracesFromFile(path_prefix);
  ASSERT_EQ(6, traces.size());
  EXPECT_TRUE(traces[0].http_streamed_trace_segment().has_request_headers());
  EXPECT_EQ("hello", traces[1].http_streamed_trace_segment().request_body_chunk().as_bytes());
  EXPECT_TRUE(traces[2].http_streamed_trace_segment().has_request_trailers());
  EXPECT_TRUE(traces[3].http_streamed_trace_segment().has_response_headers());
  EXPECT_EQ("world", traces[4].http_streamed_trace_segment().response_body_chunk().as_bytes());
  EXPECT_TRUE(traces[5].http_streamed_trace_segment().has_response_trailers());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

// Verify a static configuration with a response header matcher, writing to a streamed file per tap
// sink. This verifies request buffering.
TEST_P(TapIntegrationTest, StaticFilePerTapStreamingWithRequestBuffering) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.tap
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.tap.v2alpha.Tap
  common_config:
    static_config:
      match_config:
        http_response_headers_match:
          headers:
            - name: bar
              exact_match: baz
      output_config:
        streaming: true
        sinks:
          - format: PROTO_BINARY_LENGTH_DELIMITED
            file_per_tap:
              path_prefix: {}
)EOF";

  const std::string path_prefix = getTempPathPrefix();
  initializeFilter(fmt::format(filter_config, path_prefix));

  // Initial request/response with tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {"hello"}, &request_trailers_, response_headers_tap_,
              {"world"}, &response_trailers_);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  std::vector<envoy::data::tap::v2alpha::TraceWrapper> traces = readTracesFromFile(path_prefix);
  ASSERT_EQ(6, traces.size());
  EXPECT_TRUE(traces[0].http_streamed_trace_segment().has_request_headers());
  EXPECT_EQ("hello", traces[1].http_streamed_trace_segment().request_body_chunk().as_bytes());
  EXPECT_TRUE(traces[2].http_streamed_trace_segment().has_request_trailers());
  EXPECT_TRUE(traces[3].http_streamed_trace_segment().has_response_headers());
  EXPECT_EQ("world", traces[4].http_streamed_trace_segment().response_body_chunk().as_bytes());
  EXPECT_TRUE(traces[5].http_streamed_trace_segment().has_response_trailers());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

} // namespace
} // namespace Envoy
