#include <algorithm>
#include <iostream>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::HttpTrailers;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::service::ext_proc::v3::TrailersResponse;
using Extensions::HttpFilters::ExternalProcessing::HasHeader;
using Extensions::HttpFilters::ExternalProcessing::HasNoHeader;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::SingleHeaderValueIs;

using Http::LowerCaseString;

struct ConfigOptions {
  bool downstream_filter = true;
  bool failure_mode_allow = false;
  int64_t timeout = 900000000;
  std::string cluster = "ext_proc_server_0";
};

struct ExtProcHttpTestParams {
  Network::Address::IpVersion version;
  Http::CodecType downstream_protocol;
  Http::CodecType upstream_protocol;
};

class ExtProcHttpClientIntegrationTest : public testing::TestWithParam<ExtProcHttpTestParams>,
                                         public HttpIntegrationTest {
public:
  ExtProcHttpClientIntegrationTest()
      : HttpIntegrationTest(GetParam().downstream_protocol, GetParam().version) {}
  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate "upstreams" for ExtProc side stream servers
    for (int i = 0; i < side_stream_count_; ++i) {
      http_side_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig(ConfigOptions config_option = {},
                        const std::vector<std::pair<int, int>>& cluster_endpoints = {{0, 1},
                                                                                     {1, 1}}) {
    int total_cluster_endpoints = 0;
    std::for_each(
        cluster_endpoints.begin(), cluster_endpoints.end(),
        [&total_cluster_endpoints](const auto& item) { total_cluster_endpoints += item.second; });
    ASSERT_EQ(total_cluster_endpoints, side_stream_count_);

    config_helper_.addConfigModifier([this, cluster_endpoints, config_option](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Ensure "HTTP2 with no prior knowledge."
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

      // Clusters for ExtProc servers, starting by copying an existing cluster.
      for (const auto& [cluster_id, endpoint_count] : cluster_endpoints) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        std::string cluster_name = absl::StrCat("ext_proc_server_", cluster_id);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
        ASSERT_EQ(server_cluster->load_assignment().endpoints_size(), 1);
        auto* endpoints = server_cluster->mutable_load_assignment()->mutable_endpoints(0);
        ASSERT_EQ(endpoints->lb_endpoints_size(), 1);
        for (int i = 1; i < endpoint_count; ++i) {
          auto* new_lb_endpoint = endpoints->add_lb_endpoints();
          new_lb_endpoint->MergeFrom(endpoints->lb_endpoints(0));
        }
      }

      auto* http_uri =
          proto_config_.mutable_http_service()->mutable_http_service()->mutable_http_uri();
      http_uri->set_uri("ext_proc_server_0:9000");
      http_uri->set_cluster(config_option.cluster);
      http_uri->mutable_timeout()->set_nanos(config_option.timeout);

      if (config_option.failure_mode_allow) {
        proto_config_.set_failure_mode_allow(true);
      }
      std::string ext_proc_filter_name = "envoy.filters.http.ext_proc";
      if (config_option.downstream_filter) {
        // Construct a configuration proto for our filter and then re-write it
        // to JSON so that we can add it to the overall config
        envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter
            ext_proc_filter;
        ext_proc_filter.set_name(ext_proc_filter_name);
        ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
        config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
      }
    });

    setUpstreamProtocol(GetParam().upstream_protocol);
    setDownstreamProtocol(GetParam().downstream_protocol);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithBodyAndTrailer(absl::string_view body) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);

    auto encoder_decoder = codec_client_->startRequest(headers);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, body, false);
    Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
    codec_client_->sendTrailers(*request_encoder_, request_trailers);

    return response;
  }

  void getAndCheckHttpRequest(FakeUpstream* side_stream, bool first_message = false) {
    if (first_message) {
      ASSERT_TRUE(side_stream->waitForHttpConnection(*dispatcher_, processor_connection_));
    }

    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));
    EXPECT_THAT(processor_stream_->headers(),
                SingleHeaderValueIs("content-type", "application/json"));
    EXPECT_THAT(processor_stream_->headers(), SingleHeaderValueIs(":method", "POST"));
    EXPECT_THAT(processor_stream_->headers(), HasHeader("x-request-id"));
  }

  void sendHttpResponse(ProcessingResponse& response) {
    // Sending 200 response with the ProcessingResponse JSON encoded in the body.
    std::string response_str = MessageUtil::getJsonStringFromMessageOrError(response, true, true);
    processor_stream_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
        false);
    processor_stream_->encodeData(response_str, true);
  }

  void processRequestHeadersMessage(
      FakeUpstream* side_stream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb,
      bool send_bad_resp = false) {
    getAndCheckHttpRequest(side_stream, first_message);

    if (send_bad_resp) {
      processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "400"}}, true);
      return;
    }
    // The ext_proc ProcessingRequest message is JSON encoded in the body of the HTTP message.
    std::string body = processor_stream_->body().toString();
    ProcessingRequest request;
    bool has_unknown_field;
    auto status = MessageUtil::loadFromJsonNoThrow(body, request, has_unknown_field);
    if (status.ok()) {
      ProcessingResponse response;
      auto* headers = response.mutable_request_headers();
      const bool sendReply = !cb || (*cb)(request.request_headers(), *headers);
      if (sendReply) {
        sendHttpResponse(response);
      }
    } else {
      processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "400"}}, true);
    }
  }

  void processResponseHeadersMessage(
      FakeUpstream* side_stream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    getAndCheckHttpRequest(side_stream, first_message);

    std::string body = processor_stream_->body().toString();
    ProcessingRequest request;
    bool has_unknown_field;
    auto status = MessageUtil::loadFromJsonNoThrow(body, request, has_unknown_field);
    if (status.ok()) {
      ProcessingResponse response;
      auto* headers = response.mutable_response_headers();
      const bool sendReply = !cb || (*cb)(request.response_headers(), *headers);
      if (sendReply) {
        sendHttpResponse(response);
      }
    } else {
      processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "400"}}, true);
    }
  }

  void handleUpstreamRequest() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  }

  void handleUpstreamRequestWithTrailer() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, false);
    upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"x-test-trailers", "Yes"}});
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }

  static std::vector<ExtProcHttpTestParams> getValuesForExtProcHttpTest() {
    std::vector<ExtProcHttpTestParams> ret;
    for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
      for (auto downstream_protocol : {Http::CodecType::HTTP1, Http::CodecType::HTTP2}) {
        for (auto upstream_protocol : {Http::CodecType::HTTP1, Http::CodecType::HTTP2}) {
          ExtProcHttpTestParams params;
          params.version = ip_version;
          params.downstream_protocol = downstream_protocol;
          params.upstream_protocol = upstream_protocol;
          ret.push_back(params);
        }
      }
    }
    return ret;
  }

  static std::string
  ExtProcHttpTestParamsToString(const ::testing::TestParamInfo<ExtProcHttpTestParams>& params) {
    return absl::StrCat(
        (params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
        (params.param.downstream_protocol == Http::CodecType::HTTP1 ? "HTTP1_DS_" : "HTTP2_DS_"),
        (params.param.upstream_protocol == Http::CodecType::HTTP1 ? "HTTP1_US" : "HTTP2_US"));
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  std::vector<FakeUpstream*> http_side_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
  // Number of side stream servers in the test.
  int side_stream_count_ = 2;
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ExtProcHttpClientIntegrationTest,
    testing::ValuesIn(ExtProcHttpClientIntegrationTest::getValuesForExtProcHttpTest()),
    ExtProcHttpClientIntegrationTest::ExtProcHttpTestParamsToString);

// Side stream server does not mutate the request header.
TEST_P(ExtProcHttpClientIntegrationTest, ServerNoRequestHeaderMutation) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("foo"), "yes"); });

  // The side stream get the request and sends back the response.
  processRequestHeadersMessage(http_side_upstreams_[0], true, absl::nullopt);

  // The request is sent to the upstream.
  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("foo", "yes"));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

// Side stream server does not mutate the response header.
TEST_P(ExtProcHttpClientIntegrationTest, ServerNoResponseHeaderMutation) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("foo"), "yes"); });

  // The request is sent to the upstream.
  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("foo", "yes"));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  processResponseHeadersMessage(http_side_upstreams_[0], true, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Side stream server adds and removes headers from the header request.
TEST_P(ExtProcHttpClientIntegrationTest, GetAndSetHeadersWithMutation) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(
      http_side_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":scheme", "http"}, {":method", "GET"},       {"host", "host"},
            {":path", "/"},      {"x-remove-this", "yes"}, {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  // The request is sent to the upstream.
  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));
  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

// Side stream server does not send response trigger timeout.
TEST_P(ExtProcHttpClientIntegrationTest, ServerNoResponseFilterTimeout) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(http_side_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 400 ms exceeding 200ms filter timeout.
                                 timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(400));
                                 return false;
                               });
  // ext_proc filter timeouts sends a 504 local reply depending on runtime flag.
  verifyDownstreamResponse(*response, 504);
}

// Http timeout value set to 10ms. Test HTTP timeout.
TEST_P(ExtProcHttpClientIntegrationTest, ServerResponseHttpClientTimeout) {
  ConfigOptions config_option = {};
  config_option.timeout = 10000000;
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(http_side_upstreams_[0], true,
                               [this](const HttpHeaders&, HeadersResponse&) {
                                 // Travel forward 50 ms exceeding 10ms HTTP URI timeout setting.
                                 timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(50));
                                 return true;
                               });

  // HTTP client timeouts sends a 500 local reply.
  verifyDownstreamResponse(*response, 500);
}

// Side stream server sends back 400 with fail-mode-allow set to false.
TEST_P(ExtProcHttpClientIntegrationTest, ServerSendsBackBadRequestFailClose) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      http_side_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse&) { return true; },
      true);

  verifyDownstreamResponse(*response, 500);
}

// Side stream server sends back 400 with fail-mode-allow set to true.
TEST_P(ExtProcHttpClientIntegrationTest, ServerSendsBackBadRequestFailOpen) {
  ConfigOptions config_option = {};
  config_option.failure_mode_allow = true;
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      http_side_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse&) { return true; },
      true);

  // The request is sent to the upstream.
  handleUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

// Send headers in both directions.
TEST_P(ExtProcHttpClientIntegrationTest, SentHeadersInBothDirection) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBodyAndTrailer("foo");

  processRequestHeadersMessage(
      http_side_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                                {":method", "GET"},
                                                                {"host", "host"},
                                                                {":path", "/"},
                                                                {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_raw_value("new");
        return true;
      });

  // The request is sent to the upstream.
  handleUpstreamRequestWithTrailer();
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), "foo");

  processResponseHeadersMessage(http_side_upstreams_[0], false, absl::nullopt);
  verifyDownstreamResponse(*response, 200);
}

// Wrong ext_proc filter cluster config with fail close.
TEST_P(ExtProcHttpClientIntegrationTest, WrongClusterConfigWithFailClose) {
  ConfigOptions config_option = {};
  config_option.failure_mode_allow = false;
  config_option.cluster = "foo";
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  verifyDownstreamResponse(*response, 504);
}

// Wrong ext_proc filter cluster config with fail open
TEST_P(ExtProcHttpClientIntegrationTest, WrongClusterConfigWithFailOpen) {
  ConfigOptions config_option = {};
  config_option.failure_mode_allow = true;
  config_option.cluster = "foo";
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);

  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  // The request is sent to the upstream.
  handleUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
