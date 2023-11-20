#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"

#include "test/extensions/common/tap/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

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
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {

    // Also use HTTP/2 for upstream so that we can fully test trailers.
    setUpstreamProtocol(Http::CodecType::HTTP2);

    Envoy::Logger::DelegatingLogSinkSharedPtr sink_ptr = Envoy::Logger::Registry::getSink();
    sink_ptr->setShouldEscape(false);
  }

  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }

  const envoy::config::core::v3::HeaderValue*
  findHeader(const std::string& key,
             const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& headers) {
    for (const auto& header : headers) {
      if (header.key() == key) {
        return &header;
      }
    }

    return nullptr;
  }

  std::pair<Http::RequestEncoder*, IntegrationStreamDecoderPtr>
  startRequest(const Http::TestRequestHeaderMapImpl& request_headers,
               const std::vector<std::string>& request_body_chunks,
               const Http::TestRequestTrailerMapImpl* request_trailers,
               IntegrationCodecClient* codec_client) {
    if (!request_trailers && request_body_chunks.empty()) {
      // Headers only request - no encoder needed as no data
      return {nullptr, codec_client->makeHeaderOnlyRequest(request_headers)};
    }

    auto encoder_decoder = codec_client->startRequest(request_headers);
    return {&encoder_decoder.first, std::move(encoder_decoder.second)};
  }

  void encodeRequest(const std::vector<std::string>& request_body_chunks,
                     const Http::TestRequestTrailerMapImpl* request_trailers,
                     Http::RequestEncoder* encoder) {
    if (!encoder || (!request_trailers && request_body_chunks.empty())) {
      return;
    }

    // Encode each chunk of body data
    for (size_t i = 0; i < request_body_chunks.size(); i++) {
      Buffer::OwnedImpl data(request_body_chunks[i]);
      bool endStream = i == (request_body_chunks.size() - 1) && !request_trailers;
      encoder->encodeData(data, endStream);
    }

    // Encode trailers if they exist
    if (request_trailers) {
      encoder->encodeTrailers(*request_trailers);
    }
  }

  void encodeResponse(const Http::TestResponseHeaderMapImpl& response_headers,
                      const std::vector<std::string>& response_body_chunks,
                      const Http::TestResponseTrailerMapImpl* response_trailers,
                      FakeStream* upstream_request, IntegrationStreamDecoderPtr& decoder) {
    upstream_request->encodeHeaders(response_headers,
                                    !response_trailers && response_body_chunks.empty());

    for (size_t i = 0; i < response_body_chunks.size(); i++) {
      Buffer::OwnedImpl data(response_body_chunks[i]);
      bool endStream = i == (response_body_chunks.size() - 1) && !response_trailers;
      upstream_request->encodeData(data, endStream);
    }

    if (response_trailers) {
      upstream_request->encodeTrailers(*response_trailers);
    }

    ASSERT_TRUE(decoder->waitForEndStream());
  }

  void makeRequest(const Http::TestRequestHeaderMapImpl& request_headers,
                   const std::vector<std::string>& request_body_chunks,
                   const Http::TestRequestTrailerMapImpl* request_trailers,
                   const Http::TestResponseHeaderMapImpl& response_headers,
                   const std::vector<std::string>& response_body_chunks,
                   const Http::TestResponseTrailerMapImpl* response_trailers) {
    auto [encoder, decoder] =
        startRequest(request_headers, request_body_chunks, request_trailers, codec_client_.get());
    encodeRequest(request_body_chunks, request_trailers, encoder);
    waitForNextUpstreamRequest();
    encodeResponse(response_headers, response_body_chunks, response_trailers,
                   upstream_request_.get(), decoder);
  }

  void startAdminRequest(const std::string& admin_request_yaml) {
    const Http::TestRequestHeaderMapImpl admin_request_headers{
        {":method", "POST"}, {":path", "/tap"}, {":scheme", "http"}, {":authority", "host"}};
    WAIT_FOR_LOG_CONTAINS("debug", "New tap installed on all workers.", {
      admin_client_ = makeHttpConnection(makeClientConnection(lookupPort("admin")));
      admin_response_ =
          admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml);
      admin_response_->waitForHeaders();
      EXPECT_EQ("200", admin_response_->headers().getStatusValue());
      EXPECT_FALSE(admin_response_->complete());
    });
  }

  std::string getTempPathPrefix() {
    const std::string path_prefix = TestEnvironment::temporaryDirectory() + "/tap_integration_" +
                                    testing::UnitTest::GetInstance()->current_test_info()->name();
    TestEnvironment::createPath(path_prefix);
    return path_prefix + "/";
  }

  void verifyStaticFilePerTap(const std::string& filter_config) {
    const std::string path_prefix = getTempPathPrefix();
    initializeFilter(fmt::format(fmt::runtime(filter_config), path_prefix));

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

    envoy::data::tap::v3::TraceWrapper trace;
    TestUtility::loadFromFile(*pb_file, trace, *api_);
    EXPECT_TRUE(trace.has_http_buffered_trace());
  }

  /**
   * parseLengthDelimited parses a PROTO_BINARY_LENGTH_DELIMITED format admin response body
   * containing consecutive messages of type T into a vector of messages of type T.
   */
  template <typename T>
  void parseLengthDelimited(IntegrationStreamDecoder* admin_response, std::vector<T>& messages) {
    const uint8_t* body_data = reinterpret_cast<const uint8_t*>(admin_response->body().data());
    uint64_t body_size = admin_response->body().size();
    Protobuf::io::CodedInputStream coded_stream(body_data, body_size);

    while (true) {
      uint64_t message_size;
      if (!coded_stream.ReadVarint64(&message_size)) {
        break;
      }

      messages.emplace_back();

      auto limit = coded_stream.PushLimit(message_size);
      EXPECT_TRUE(messages.back().ParseFromCodedStream(&coded_stream));
      coded_stream.PopLimit(limit);
    }
  }

  const Http::TestRequestHeaderMapImpl request_headers_tap_{{":method", "GET"},
                                                            {":path", "/"},
                                                            {":scheme", "http"},
                                                            {":authority", "host"},
                                                            {"foo", "bar"}};

  const Http::TestRequestHeaderMapImpl request_headers_no_tap_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  const Http::TestRequestTrailerMapImpl request_trailers_{{"foo_trailer", "bar"}};

  const Http::TestResponseHeaderMapImpl response_headers_tap_{{":status", "200"}, {"bar", "baz"}};

  const Http::TestResponseHeaderMapImpl response_headers_no_tap_{{":status", "200"}};

  const Http::TestResponseTrailerMapImpl response_trailers_{{"bar_trailer", "baz"}};

  const std::string admin_filter_config_ =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
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
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
  common_config:
    static_config:
      match:
        any_match: true
      output_config:
        sinks:
          - format: PROTO_BINARY
            file_per_tap:
              path_prefix: {}
)EOF";

  verifyStaticFilePerTap(filter_config);
}

// Verify the match field takes precedence over the deprecated match_config field.
TEST_P(TapIntegrationTest, DEPRECATED_FEATURE_TEST(StaticFilePerTapWithMatchConfigAndMatch)) {
  const std::string filter_config =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
  common_config:
    static_config:
      # match_config should be ignored by the match field.
      match_config:
        not_match:
          any_match: true
      match:
        any_match: true
      output_config:
        sinks:
          - format: PROTO_BINARY
            file_per_tap:
              path_prefix: {}
)EOF";

  verifyStaticFilePerTap(filter_config);
}

// Verify the deprecated match_config field.
TEST_P(TapIntegrationTest, DEPRECATED_FEATURE_TEST(StaticFilePerTapWithMatchConfig)) {
  const std::string filter_config =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
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

  verifyStaticFilePerTap(filter_config);
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
  match:
    or_match:
      rules:
        - http_request_headers_match:
            headers:
              - name: foo
                string_match:
                  exact: bar
        - http_response_headers_match:
            headers:
              - name: bar
                string_match:
                  exact: baz
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
  // The above admin tap close can race with the request that follows and we don't have any way
  // to synchronize it, so just count the number of taps after the request for use below.
  const auto current_tapped = test_server_->counter("http.config_test.tap.rq_tapped")->value();

  // Setup the tap again and leave it open.
  startAdminRequest(admin_request_yaml);

  // Do a request which should tap, matching on request headers.
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  // Wait for the tap message.
  admin_response_->waitForBodyData(1);
  envoy::data::tap::v3::TraceWrapper trace;
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
  match:
    and_match:
      rules:
        - http_request_headers_match:
            headers:
              - name: foo
                string_match:
                  exact: bar
        - http_response_headers_match:
            headers:
              - name: bar
                string_match:
                  exact: baz
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
  EXPECT_EQ(current_tapped + 3UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
  test_server_->waitForGaugeEq("http.admin.downstream_rq_active", 0);
}

// Make sure that an admin tap works correctly across an LDS reload.
TEST_P(TapIntegrationTest, AdminLdsReload) {
  initializeFilter(admin_filter_config_);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match:
    and_match:
      rules:
        - http_request_trailers_match:
            headers:
              - name: foo_trailer
                string_match:
                  exact: bar
        - http_response_trailers_match:
            headers:
              - name: bar_trailer
                string_match:
                  exact: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.prependFilter(admin_filter_config_);
  new_config_helper.renameListener("foo");
  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  registerTestServerPorts({"http"});

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {}, &request_trailers_, response_headers_no_tap_, {},
              &response_trailers_);

  envoy::data::tap::v3::TraceWrapper trace;
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_EQ("bar",
            findHeader("foo_trailer", trace.http_buffered_trace().request().trailers())->value());
  EXPECT_EQ("baz",
            findHeader("bar_trailer", trace.http_buffered_trace().response().trailers())->value());
  admin_client_->close();
}

// Verify the content and ordering of buffered tap traces
TEST_P(TapIntegrationTest, AdminBufferedTapContent) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  const int num_req = 4; // # of requests to buffer before responding

  initializeFilter(admin_filter_config_);

  // Initial request / response with no tap
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          max_traces: {}
  )EOF";

  startAdminRequest(fmt::format(admin_request_yaml, num_req));

  for (int i = 0; i < num_req; i++) {
    makeRequest(request_headers_no_tap_, {{std::to_string(i) + "request"}}, nullptr,
                response_headers_no_tap_, {{std::to_string(i) + "response"}}, nullptr);
  }

  auto result = admin_response_->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  std::vector<TraceWrapper> traces;
  parseLengthDelimited(admin_response_.get(), traces);

  // Assert we buffered num_req traces as required
  EXPECT_EQ(traces.size(), num_req);

  for (size_t i = 0; i < traces.size(); i++) {
    const auto& trace = traces[i].http_buffered_trace();
    EXPECT_FALSE(trace.request().body().truncated());
    EXPECT_FALSE(trace.response().body().truncated());
    EXPECT_EQ(std::to_string(i) + "request", std::string(trace.request().body().as_bytes()));
    EXPECT_EQ(std::to_string(i) + "response", std::string(trace.response().body().as_bytes()));
  }

  admin_response_->clearBody();
  admin_client_->close();
}

// Verify handling of concurrent requests once tap is listening
TEST_P(TapIntegrationTest, AdminBufferedTapConcurrent) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  const int num_streams = 4;

  initializeFilter(admin_filter_config_);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          max_traces: {}
  )EOF";

  startAdminRequest(fmt::format(admin_request_yaml, num_streams));

  FakeStreamPtr upstream_reqs[num_streams];
  IntegrationStreamDecoderPtr decoders[num_streams];
  Http::RequestEncoder* encoders[num_streams];

  for (int i = 0; i < num_streams; i++) {
    auto [encoder, decoder] =
        startRequest(request_headers_no_tap_, {{std::to_string(i)}}, nullptr, codec_client_.get());
    encoders[i] = encoder;
    decoders[i] = std::move(decoder);
  }

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  for (int i = 0; i < num_streams; i++) {
    encodeRequest({{std::to_string(i)}}, nullptr, encoders[i]);
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_reqs[i]));

    // Wait for the request to be fully sent
    auto result = upstream_reqs[i]->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
  }

  for (int i = 0; i < num_streams; i++) {
    encodeResponse(response_headers_no_tap_, {{std::to_string(i)}}, nullptr, upstream_reqs[i].get(),
                   decoders[i]);
  }

  auto result = admin_response_->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  std::vector<TraceWrapper> traces;
  parseLengthDelimited(admin_response_.get(), traces);

  // Assert we buffered num_req traces as required
  EXPECT_EQ(traces.size(), num_streams);

  bool received[num_streams]{false};
  for (const auto& traceproto : traces) {
    const auto& trace = traceproto.http_buffered_trace();
    int response_idx = std::stoi(std::string(trace.response().body().as_bytes()));
    int request_idx = std::stoi(std::string(trace.request().body().as_bytes()));

    EXPECT_EQ(response_idx, request_idx);
    EXPECT_LT(response_idx, num_streams);
    EXPECT_GE(response_idx, 0);
    EXPECT_FALSE(received[response_idx]);
    received[response_idx] = true; // record the trace number we received
  }

  for (bool el : received) {
    EXPECT_TRUE(el);
  }

  admin_response_->clearBody();
  admin_client_->close();
}

// Verify handling of timeout expiry
TEST_P(TapIntegrationTest, AdminBufferedTapTimeout) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  const int num_req = 4;    // # of requests to buffer before responding
  const int timeout = 1000; // milliseconds

  initializeFilter(admin_filter_config_);

  // Initial request / response with no tap
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          max_traces: {}
          timeout: {}s
  )EOF";

  startAdminRequest(fmt::format(admin_request_yaml, num_req, static_cast<double>(timeout) / 1000));

  for (int i = 0; i < num_req; i++) {
    makeRequest(request_headers_no_tap_, {{std::to_string(i) + "request"}}, nullptr,
                response_headers_no_tap_, {{std::to_string(i) + "response"}}, nullptr);
    timeSystem().advanceTimeWaitImpl(
        std::chrono::milliseconds(timeout * 2)); // force the timeout to expire
  }

  auto result = admin_response_->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  std::vector<TraceWrapper> traces;
  parseLengthDelimited(admin_response_.get(), traces);

  // Assert we buffered at most one trace
  EXPECT_LE(traces.size(), 1);

  // check data in buffer is the first data sent
  const auto& trace = traces[0].http_buffered_trace();
  EXPECT_FALSE(trace.request().body().truncated());
  EXPECT_FALSE(trace.response().body().truncated());
  EXPECT_EQ("0request", std::string(trace.request().body().as_bytes()));
  EXPECT_EQ("0response", std::string(trace.response().body().as_bytes()));

  admin_response_->clearBody();
  admin_client_->close();
}

// Verify filling the buffer before the timeout expires
TEST_P(TapIntegrationTest, AdminBufferedTapLongTimeout) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  using namespace std::chrono_literals;
  const int num_req = 4; // # of requests to buffer before responding

  initializeFilter(admin_filter_config_);

  // Initial request / response with no tap
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          timeout: 60s
          max_traces: {}
  )EOF";

  startAdminRequest(fmt::format(admin_request_yaml, num_req));

  // Make num_req tapped requests
  for (size_t i = 0; i < num_req; i++) {
    makeRequest(request_headers_no_tap_, {{std::to_string(i) + "request"}}, nullptr,
                response_headers_no_tap_, {{std::to_string(i) + "response"}}, nullptr);
  }

  auto result = admin_response_->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  std::vector<TraceWrapper> traces;
  parseLengthDelimited(admin_response_.get(), traces);

  // Assert we buffered num_req traces as required
  EXPECT_EQ(traces.size(), num_req);

  admin_response_->clearBody();
  admin_client_->close();
}

// Verify that consecutive tap requests use fresh buffers
TEST_P(TapIntegrationTest, AdminBufferedTapConsecutive) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  const int num_req = 1; // # of requests to buffer before responding

  initializeFilter(admin_filter_config_);

  // Initial request / response with no tap
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          max_traces: {}
  )EOF";

  {
    startAdminRequest(fmt::format(admin_request_yaml, num_req));

    for (int i = 0; i < num_req; i++) {
      makeRequest(request_headers_no_tap_, {{std::to_string(i) + "request"}}, nullptr,
                  response_headers_no_tap_, {{std::to_string(i) + "response"}}, nullptr);
    }

    auto result = admin_response_->waitForEndStream();
    RELEASE_ASSERT(result, result.message());

    std::vector<TraceWrapper> traces;
    parseLengthDelimited(admin_response_.get(), traces);

    // Assert we buffered num_req traces as required
    EXPECT_EQ(traces.size(), num_req);

    for (size_t i = 0; i < traces.size(); i++) {
      const auto& trace = traces[i].http_buffered_trace();
      EXPECT_FALSE(trace.request().body().truncated());
      EXPECT_FALSE(trace.response().body().truncated());
      EXPECT_EQ(std::to_string(i) + "request", std::string(trace.request().body().as_bytes()));
      EXPECT_EQ(std::to_string(i) + "response", std::string(trace.response().body().as_bytes()));
    }

    admin_response_->clearBody();
    admin_client_->close();
  }
  {
    startAdminRequest(fmt::format(admin_request_yaml, num_req));

    for (int i = 0; i < num_req; i++) {
      makeRequest(request_headers_no_tap_, {{std::to_string(i) + "request2"}}, nullptr,
                  response_headers_no_tap_, {{std::to_string(i) + "response2"}}, nullptr);
    }

    auto result = admin_response_->waitForEndStream();
    RELEASE_ASSERT(result, result.message());
    std::vector<TraceWrapper> traces;
    parseLengthDelimited(admin_response_.get(), traces);

    // Assert we buffered num_req traces as required
    EXPECT_EQ(traces.size(), num_req);

    for (size_t i = 0; i < traces.size(); i++) {
      const auto& trace = traces[i].http_buffered_trace();
      EXPECT_FALSE(trace.request().body().truncated());
      EXPECT_FALSE(trace.response().body().truncated());
      EXPECT_EQ(std::to_string(i) + "request2", std::string(trace.request().body().as_bytes()));
      EXPECT_EQ(std::to_string(i) + "response2", std::string(trace.response().body().as_bytes()));
    }

    admin_response_->clearBody();
    admin_client_->close();
  }
}

// Verify that no data is returned until the buffer is filled
TEST_P(TapIntegrationTest, AdminBufferedTapBuffering) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  using namespace std::chrono_literals;
  const int num_req = 4; // # of requests to buffer before responding
  const auto sleep_duration = 100ms;

  initializeFilter(admin_filter_config_);

  // Initial request / response with no tap
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          max_traces: {}
  )EOF";

  startAdminRequest(fmt::format(admin_request_yaml, num_req));

  // Make num_req tapped requests
  for (size_t i = 0; i < num_req; i++) {
    // Verify that no body data has been received yet
    EXPECT_EQ(std::string(""), admin_response_->body());
    makeRequest(request_headers_no_tap_, {{std::to_string(i) + "request"}}, nullptr,
                response_headers_no_tap_, {{std::to_string(i) + "response"}}, nullptr);
    timeSystem().advanceTimeWaitImpl(sleep_duration);
  }

  auto result = admin_response_->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  std::vector<TraceWrapper> traces;
  parseLengthDelimited(admin_response_.get(), traces);

  // Assert we buffered num_req traces as required
  EXPECT_EQ(traces.size(), num_req);

  admin_response_->clearBody();
  admin_client_->close();
}

// Verify that a response is returned on timeout if no traces are matched
TEST_P(TapIntegrationTest, AdminBufferedTapEmptyResponse) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  using namespace std::chrono_literals;
  const int num_req = 2; // # of requests to buffer before responding

  initializeFilter(admin_filter_config_);

  // Initial request / response with no tap
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          max_traces: {}
          timeout: {}s
  )EOF";

  startAdminRequest(fmt::format(admin_request_yaml, num_req, "1"));

  auto result = admin_response_->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  std::vector<TraceWrapper> traces;
  parseLengthDelimited(admin_response_.get(), traces);

  // Assert we buffered no traces
  EXPECT_EQ(traces.size(), 0);

  admin_response_->clearBody();
  admin_client_->close();
}

// Verify Sending more traces than expected still returns the expected size buffer
TEST_P(TapIntegrationTest, AdminBufferedTapOverBuffering) {
  using TraceWrapper = envoy::data::tap::v3::TraceWrapper;
  using namespace std::chrono_literals;
  const int num_req = 4; // # of requests to buffer before responding

  initializeFilter(admin_filter_config_);

  // Initial request / response with no tap
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {}, nullptr, response_headers_no_tap_, {}, nullptr);

  constexpr absl::string_view admin_request_yaml = R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - format: PROTO_BINARY_LENGTH_DELIMITED
        buffered_admin:
          max_traces: {}
  )EOF";

  startAdminRequest(fmt::format(admin_request_yaml, num_req));

  // Make num_req tapped requests
  for (size_t i = 0; i < num_req * 2; i++) {
    makeRequest(request_headers_no_tap_, {{std::to_string(i) + "request"}}, nullptr,
                response_headers_no_tap_, {{std::to_string(i) + "response"}}, nullptr);
  }

  auto result = admin_response_->waitForEndStream();
  RELEASE_ASSERT(result, result.message());

  std::vector<TraceWrapper> traces;
  parseLengthDelimited(admin_response_.get(), traces);

  // Assert we buffered num_req traces as required
  EXPECT_EQ(traces.size(), num_req);

  admin_response_->clearBody();
  admin_client_->close();
}

// Verify both request and response trailer matching works.
TEST_P(TapIntegrationTest, AdminTrailers) {
  initializeFilter(admin_filter_config_);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match:
    and_match:
      rules:
        - http_request_trailers_match:
            headers:
              - name: foo_trailer
                string_match:
                  exact: bar
        - http_response_trailers_match:
            headers:
              - name: bar_trailer
                string_match:
                  exact: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {}, &request_trailers_, response_headers_no_tap_, {},
              &response_trailers_);

  envoy::data::tap::v3::TraceWrapper trace;
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
  match:
    any_match: true
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_no_tap_, {{"hello"}}, nullptr, response_headers_no_tap_, {{"world"}},
              nullptr);
  envoy::data::tap::v3::TraceWrapper trace;
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
  match:
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
  envoy::data::tap::v3::TraceWrapper trace;
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
  match:
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
  envoy::data::tap::v3::TraceWrapper trace;
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
  constexpr absl::string_view filter_config =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
  common_config:
    static_config:
      match:
        http_request_headers_match:
          headers:
            - name: foo
              string_match:
                exact: bar
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

  std::vector<envoy::data::tap::v3::TraceWrapper> traces =
      Extensions::Common::Tap::readTracesFromPath(path_prefix);
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
  constexpr absl::string_view filter_config =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
  common_config:
    static_config:
      match:
        http_response_headers_match:
          headers:
            - name: bar
              string_match:
                exact: baz
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

  std::vector<envoy::data::tap::v3::TraceWrapper> traces =
      Extensions::Common::Tap::readTracesFromPath(path_prefix);
  ASSERT_EQ(6, traces.size());
  EXPECT_TRUE(traces[0].http_streamed_trace_segment().has_request_headers());
  EXPECT_EQ("hello", traces[1].http_streamed_trace_segment().request_body_chunk().as_bytes());
  EXPECT_TRUE(traces[2].http_streamed_trace_segment().has_request_trailers());
  EXPECT_TRUE(traces[3].http_streamed_trace_segment().has_response_headers());
  EXPECT_EQ("world", traces[4].http_streamed_trace_segment().response_body_chunk().as_bytes());
  EXPECT_TRUE(traces[5].http_streamed_trace_segment().has_response_trailers());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

// Verify option record_headers_received_time
// when a request header is matched in a static configuration
TEST_P(TapIntegrationTest, StaticFilePerHttpBufferTraceTapForRequest) {
  constexpr absl::string_view filter_config =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
  common_config:
    static_config:
      match:
        http_request_headers_match:
          headers:
            - name: foo
              string_match:
                exact: bar
      output_config:
        sinks:
          - format: PROTO_BINARY_LENGTH_DELIMITED
            file_per_tap:
              path_prefix: {}
  record_headers_received_time: true
)EOF";

  const std::string path_prefix = getTempPathPrefix();
  initializeFilter(fmt::format(filter_config, path_prefix));

  // Initial request/response with tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {"hello"}, &request_trailers_, response_headers_no_tap_,
              {"world"}, &response_trailers_);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  std::vector<envoy::data::tap::v3::TraceWrapper> traces =
      Extensions::Common::Tap::readTracesFromPath(path_prefix);
  ASSERT_EQ(1, traces.size());
  EXPECT_TRUE(traces[0].has_http_buffered_trace());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

// Verify option record_downstream_connection
// when a request header is matched in a static configuration
TEST_P(TapIntegrationTest, StaticFilePerHttpBufferTraceTapDownstreamConnection) {
  constexpr absl::string_view filter_config =
      R"EOF(
name: tap
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
  common_config:
    static_config:
      match:
        http_request_headers_match:
          headers:
            - name: foo
              string_match:
                exact: bar
      output_config:
        sinks:
          - format: PROTO_BINARY_LENGTH_DELIMITED
            file_per_tap:
              path_prefix: {}
  record_downstream_connection: true
)EOF";

  const std::string path_prefix = getTempPathPrefix();
  initializeFilter(fmt::format(filter_config, path_prefix));

  // Initial request/response with tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  makeRequest(request_headers_tap_, {"hello"}, &request_trailers_, response_headers_no_tap_,
              {"world"}, &response_trailers_);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  std::vector<envoy::data::tap::v3::TraceWrapper> traces =
      Extensions::Common::Tap::readTracesFromPath(path_prefix);
  ASSERT_EQ(1, traces.size());
  EXPECT_TRUE(traces[0].has_http_buffered_trace());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

// Verify that body matching works.
TEST_P(TapIntegrationTest, AdminBodyMatching) {
  initializeFilter(admin_filter_config_);

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match:
    and_match:
      rules:
        - http_request_generic_body_match:
            patterns:
              - string_match: request
        - http_response_generic_body_match:
            patterns:
              - string_match: response
  output_config:
    sinks:
      - format: JSON_BODY_AS_STRING
        streaming_admin: {}
)EOF";

  startAdminRequest(admin_request_yaml);

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  // Should not tap, request and response body do not match.
  makeRequest(request_headers_no_tap_, {{"This is test payload"}}, nullptr,
              response_headers_no_tap_, {{"This is test payload"}}, nullptr);
  // Should not tap, request matches but response body does not match.
  makeRequest(request_headers_no_tap_, {{"This is request payload"}}, nullptr,
              response_headers_no_tap_, {{"This is test payload"}}, nullptr);
  // Should tap, request and response body match.
  makeRequest(request_headers_no_tap_, {{"This is request payload"}}, nullptr,
              response_headers_no_tap_, {{"This is resp"}, {"onse payload"}}, nullptr);

  envoy::data::tap::v3::TraceWrapper trace;
  admin_response_->waitForBodyData(1);
  TestUtility::loadFromYaml(admin_response_->body(), trace);
  EXPECT_NE(std::string::npos,
            trace.http_buffered_trace().request().body().as_string().find("request"));
  EXPECT_NE(std::string::npos,
            trace.http_buffered_trace().response().body().as_string().find("response"));

  admin_client_->close();
}

} // namespace
} // namespace Envoy
