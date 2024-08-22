#include <algorithm>
#include <iostream>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using Extensions::HttpFilters::ExternalProcessing::SingleHeaderValueIs;
using Extensions::HttpFilters::ExternalProcessing::HasNoHeader;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;

using Http::LowerCaseString;

struct ConfigOptions {
  bool http1_codec = false;
  bool downstream_filter = true;
  bool failure_mode_allow = false;
};

class ExtProcHttpClientIntegrationTest : public HttpIntegrationTest,
                                         public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
protected:
  ExtProcHttpClientIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

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

  std::string default_http_config_ = R"EOF(
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SKIP"
  )EOF";

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

      TestUtility::loadFromYaml(default_http_config_, proto_config_);
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

      // Parameterize with defer processing to prevent bit rot as filter made
      // assumptions of data flow, prior relying on eager processing.
      config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                        deferredProcessing() ? "true" : "false");
    });

    if (config_option.http1_codec) {
      setUpstreamProtocol(Http::CodecType::HTTP1);
      setDownstreamProtocol(Http::CodecType::HTTP1);
    } else {
      setUpstreamProtocol(Http::CodecType::HTTP2);
      setDownstreamProtocol(Http::CodecType::HTTP2);
    }
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

  void processRequestHeadersMessage(
      FakeUpstream* side_stream, bool send_bad_resp,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ASSERT_TRUE(side_stream->waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));

    if (send_bad_resp) {
      processor_stream_->encodeHeaders(
          Http::TestResponseHeaderMapImpl{{":status", "400"}}, false);
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
        // Sending 200 response with the ProcessingResponse JSON encoded in the body.
        std::string response_str = MessageUtil::getJsonStringFromMessageOrError(response, true, true);
        processor_stream_->encodeHeaders(
            Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
        processor_stream_->encodeData(response_str, true);
      }
    } else {
      processor_stream_->encodeHeaders(
          Http::TestResponseHeaderMapImpl{{":status", "400"}}, false);
    }
  }

  void handleUpstreamRequest() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  std::vector<FakeUpstream*> http_side_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
  // Number of side stream servers in the test.
  int side_stream_count_ = 2;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, ExtProcHttpClientIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

// Side stream server does not mutate the header request.
TEST_P(ExtProcHttpClientIntegrationTest, ServerNoHeaderMutation) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("foo"), "yes"); });

  // The side stream get the request and sends back the response.
  processRequestHeadersMessage(http_side_upstreams_[0], false,  absl::nullopt);

  // The request is sent to the upstream.
  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("foo", "yes"));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

// Side stream server adds and removes headers from the header request.
TEST_P(ExtProcHttpClientIntegrationTest, GetAndSetHeadersWithMutation) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processRequestHeadersMessage(
      http_side_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
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
TEST_P(ExtProcHttpClientIntegrationTest, ServerNoResponseTimeout) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      http_side_upstreams_[0], false, [this](const HttpHeaders&, HeadersResponse&) {
        // Travel forward 400 ms
        timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(400));
        return false;
      });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 504);
}

// Side stream server sends back 400 with fail-mode-allow set to false.
TEST_P(ExtProcHttpClientIntegrationTest, ServerSendsBackBadRequestFailClose) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      http_side_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse&) {
        return true;
      });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 504);
}

// Side stream server sends back 400 with fail-mode-allow set to true.
TEST_P(ExtProcHttpClientIntegrationTest, ServerSendsBackBadRequestFailOpen) {
  ConfigOptions config_option = {};
  config_option.failure_mode_allow = true;
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processRequestHeadersMessage(
      http_side_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse&) {
        return true;
      });

  // The request is sent to the upstream.
  handleUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  verifyDownstreamResponse(*response, 200);
}

} // namespace Envoy
