#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/network/address.h"

#include "source/common/http/match_delegate/config.h"
#include "source/common/http/matching/inputs.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/integration/filters/server_factory_context_filter_config.pb.h"
#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/helloworld.pb.h"
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

class CompositeFilterSeverContextIntegrationTest
    : public HttpIntegrationTest,
      public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
public:
  CompositeFilterSeverContextIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create separate "upstreams" for test gRPC servers
    for (int i = 0; i < 2; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void TearDown() override {
    if (connection_) {
      ASSERT_TRUE(connection_->close());
      ASSERT_TRUE(connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

      // Clusters for test gRPC servers, starting by copying an existing cluster
      for (size_t i = 0; i < grpc_upstreams_.size(); ++i) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        std::string cluster_name = absl::StrCat("test_server_", i);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
      }
      // Load configuration of the server from YAML and use a helper to add a grpc_service
      // stanza pointing to the cluster that we just made
      setGrpcService(*filter_config_.mutable_grpc_service(), "test_server_0",
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
    // Add the filter to the loaded hcm.
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        hcm_config;
    config_helper_.loadHttpConnectionManager(hcm_config);
    auto* match_delegate_filter = hcm_config.add_http_filters();
    match_delegate_filter->set_name("envoy.filters.http.match_delegate");

    // Build extension config with composite filter inside.
    envoy::extensions::common::matching::v3::ExtensionWithMatcher extension_config;
    extension_config.mutable_extension_config()->set_name("composite");
    envoy::extensions::filters::http::composite::v3::Composite composite_config;
    extension_config.mutable_extension_config()->mutable_typed_config()->PackFrom(composite_config);
    auto* matcher_tree = extension_config.mutable_xds_matcher()->mutable_matcher_tree();

    // Set up the match input.
    auto* matcher_input = matcher_tree->mutable_input();
    matcher_input->set_name("request-headers");
    envoy::type::matcher::v3::HttpRequestHeaderMatchInput request_header_match_input;
    request_header_match_input.set_header_name("match-header");
    matcher_input->mutable_typed_config()->PackFrom(request_header_match_input);

    // Set up the match action with test filter as the delegated filter.
    auto* exact_match_map = matcher_tree->mutable_exact_match_map()->mutable_map();
    envoy::extensions::filters::http::composite::v3::ExecuteFilterAction test_filter_action;
    test_filter_action.mutable_typed_config()->set_name("server-factory-context-filter");
    test_filter_action.mutable_typed_config()->mutable_typed_config()->PackFrom(filter_config_);

    ::xds::type::matcher::v3::Matcher_OnMatch on_match;
    auto* on_match_action = on_match.mutable_action();
    on_match_action->set_name("composite-action");
    on_match_action->mutable_typed_config()->PackFrom(test_filter_action);

    (*exact_match_map)["match"] = on_match;

    // Finish up the construction of match_delegate_filter.
    match_delegate_filter->mutable_typed_config()->PackFrom(extension_config);

    // Now move the built filter to the front.
    for (int i = hcm_config.http_filters_size() - 1; i > 0; --i) {
      hcm_config.mutable_http_filters()->SwapElements(i, i - 1);
    }

    // Store it to hcm.
    config_helper_.storeHttpConnectionManager(hcm_config);
  }

  test::integration::filters::ServerFactoryContextFilterConfig filter_config_;
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr connection_;
  FakeStreamPtr stream_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, CompositeFilterSeverContextIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

TEST_P(CompositeFilterSeverContextIntegrationTest, BasicFlow) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  const Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                          {":path", "/somepath"},
                                                          {":scheme", "http"},
                                                          {"match-header", "match"},
                                                          {":authority", "blah"}};
  // Send request from downstream to upstream.
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Wait for side stream request.
  helloworld::HelloRequest request;
  request.set_name("hello");
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, connection_));
  ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream_));
  ASSERT_TRUE(stream_->waitForGrpcMessage(*dispatcher_, request));

  // Start the grpc side stream.
  stream_->startGrpcStream();

  // Send the side stream response.
  helloworld::HelloReply reply;
  reply.set_message("ack");
  stream_->sendGrpcMessage(reply);

  // Handle the upstream request.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Close the grpc stream.
  stream_->finishGrpcStream(Grpc::Status::Ok);

  // Verify the response from upstream to downstream.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");
}

} // namespace Envoy
