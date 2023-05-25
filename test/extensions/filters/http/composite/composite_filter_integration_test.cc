#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/http/match_delegate/config.h"
#include "source/common/http/matching/inputs.h"
#include "source/extensions/filters/http/ext_proc/config.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
class CompositeFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  CompositeFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.prependFilter(R"EOF(
  name: composite
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
    extension_config:
      name: composite
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
    xds_matcher:
      matcher_tree:
        input:
          name: request-headers
          typed_config:
            "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            header_name: match-header
        exact_match_map:
          map:
            match:
              action:
                name: composite-action
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
                  typed_config:
                    name: set-response-code
                    typed_config:
                      "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
                      code: 403
    )EOF");
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that if we don't match the match action the request is proxied as normal, while if the
// match action is hit we apply the specified filter to the stream.
TEST_P(CompositeFilterIntegrationTest, TestBasic) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  {
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  }

  {
    const Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                            {":path", "/somepath"},
                                                            {":scheme", "http"},
                                                            {"match-header", "match"},
                                                            {":authority", "blah"}};
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("403"));
  }
}

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using Extensions::HttpFilters::ExternalProcessing::HasNoHeader;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::SingleHeaderValueIs;

class CompositeFilterWithExtProcIntegrationTest
    : public HttpIntegrationTest,
      public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
public:
  CompositeFilterWithExtProcIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create separate "upstreams" for ExtProc gRPC servers
    for (int i = 0; i < 2; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  // TODO(tyxia) Important clean up.
  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

      // Clusters for ExtProc gRPC servers, starting by copying an existing cluster
      for (size_t i = 0; i < grpc_upstreams_.size(); ++i) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        std::string cluster_name = absl::StrCat("ext_proc_server_", i);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
      }
      // Load configuration of the server from YAML and use a helper to add a grpc_service
      // stanza pointing to the cluster that we just made
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server_0",
                     grpc_upstreams_[0]->localAddress());

      addFilter();

      // Parameterize with defer processing to prevent bit rot as filter made
      // assumptions of data flow, prior relying on eager processing.
      config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                        deferredProcessing() ? "true" : "false");
    });

    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  void addFilter() {
    // Set up ext_proc processing mode.
    proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
    proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
    proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
    proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::NONE);
    proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SKIP);
    proto_config_.mutable_processing_mode()->set_response_trailer_mode(ProcessingMode::SKIP);

    // Add filter to the loaded hcm.
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        hcm_config;
    config_helper_.loadHttpConnectionManager(hcm_config);
    auto* match_delegate_filter = hcm_config.add_http_filters();
    match_delegate_filter->set_name("envoy.filters.http.match_delegate");

    // Build extension config.
    envoy::extensions::common::matching::v3::ExtensionWithMatcher extension_config;
    extension_config.mutable_extension_config()->set_name("composite");
    envoy::extensions::filters::http::composite::v3::Composite composite_config;
    extension_config.mutable_extension_config()->mutable_typed_config()->PackFrom(composite_config);
    auto* matcher_tree = extension_config.mutable_xds_matcher()->mutable_matcher_tree();
    // Set up match input.
    auto* matcher_input = matcher_tree->mutable_input();
    matcher_input->set_name("request-headers");
    envoy::type::matcher::v3::HttpRequestHeaderMatchInput request_header_match_input;
    request_header_match_input.set_header_name("match-header");
    matcher_input->mutable_typed_config()->PackFrom(request_header_match_input);

    // Set up match action.
    auto* exact_match_map = matcher_tree->mutable_exact_match_map()->mutable_map();
    envoy::extensions::filters::http::composite::v3::ExecuteFilterAction ext_proc_filter_action;
    ext_proc_filter_action.mutable_typed_config()->set_name("envoy.filters.http.ext_proc");
    ext_proc_filter_action.mutable_typed_config()->mutable_typed_config()->PackFrom(proto_config_);

    ::xds::type::matcher::v3::Matcher_OnMatch on_match;
    auto* on_match_action = on_match.mutable_action();
    on_match_action->set_name("composite-action");
    on_match_action->mutable_typed_config()->PackFrom(ext_proc_filter_action);

    (*exact_match_map)["match"] = on_match;

    match_delegate_filter->mutable_typed_config()->PackFrom(extension_config);

    // Now move the built to the front.
    for (int i = hcm_config.http_filters_size() - 1; i > 0; --i) {
      hcm_config.mutable_http_filters()->SwapElements(i, i - 1);
    }

    // Store it to hcm.
    config_helper_.storeHttpConnectionManager(hcm_config);
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

  void waitForFirstMessage(FakeUpstream& grpc_upstream,
                           envoy::service::ext_proc::v3::ProcessingRequest& request) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  void handleUpstreamRequest() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }

  void processRequestHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_request_headers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* headers = response.mutable_request_headers();
    const bool sendReply = !cb || (*cb)(request.request_headers(), *headers);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseHeadersMessage(
      FakeUpstream& grpc_upstream, bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_headers());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* headers = response.mutable_response_headers();
    const bool sendReply = !cb || (*cb)(request.response_headers(), *headers);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, CompositeFilterWithExtProcIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

TEST_P(CompositeFilterWithExtProcIntegrationTest, GetAndCloseStream) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  const Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                          {":path", "/somepath"},
                                                          {":scheme", "http"},
                                                          {"match-header", "match"},
                                                          {":authority", "blah"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  envoy::service::ext_proc::v3::ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Just close the stream without doing anything
  processor_stream_->startGrpcStream();
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but closes the stream after response_headers.
TEST_P(CompositeFilterWithExtProcIntegrationTest, GetAndCloseStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  const Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                          {":path", "/somepath"},
                                                          {":scheme", "http"},
                                                          {"match-header", "match"},
                                                          {":authority", "blah"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  verifyDownstreamResponse(*response, 200);
}

TEST_P(CompositeFilterWithExtProcIntegrationTest, GetAndSetHeaders) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                    {":path", "/somepath"},
                                                    {":scheme", "http"},
                                                    {"match-header", "match"},
                                                    {":authority", "blah"}};
  request_headers.addCopy(Http::LowerCaseString("x-remove-this"), "yes");
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
        Http::TestRequestHeaderMapImpl expected_request_headers{
            {":method", "GET"},           {":path", "/somepath"}, {":scheme", "http"},
            {"match-header", "match"},    {":authority", "blah"}, {"x-remove-this", "yes"},
            {"x-forwarded-proto", "http"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_request_headers));

        auto response_header_mutation = headers_resp.mutable_response()->mutable_header_mutation();
        auto* mut1 = response_header_mutation->add_set_headers();
        mut1->mutable_header()->set_key("x-new-header");
        mut1->mutable_header()->set_value("new");
        response_header_mutation->add_remove_headers("x-remove-this");
        return true;
      });

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  processResponseHeadersMessage(
      *grpc_upstreams_[0], false, [](const HttpHeaders& headers, HeadersResponse&) {
        Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
        EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
        return true;
      });

  verifyDownstreamResponse(*response, 200);
}

} // namespace Envoy
