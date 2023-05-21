#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "source/extensions/filters/http/ext_proc/config.h"
#include "test/common/http/common.h"

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


class CompositeFilterWithExtProcIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  CompositeFilterWithExtProcIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  // void initialize() override {
  //   config_helper_.prependFilter(R"EOF(
  // name: composite
  // typed_config:
  //   "@type": type.googleapis.com/envoy.extensions.common.matching.v3.ExtensionWithMatcher
  //   extension_config:
  //     name: composite
  //     typed_config:
  //       "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.Composite
  //   xds_matcher:
  //     matcher_tree:
  //       input:
  //         name: request-headers
  //         typed_config:
  //           "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
  //           header_name: match-header
  //       exact_match_map:
  //         map:
  //           match:
  //             action:
  //               name: composite-action
  //               typed_config:
  //                 "@type": type.googleapis.com/envoy.extensions.filters.http.composite.v3.ExecuteFilterAction
  //                 typed_config:
  //                   name: set-response-code
  //                   typed_config:
  //                     "@type": type.googleapis.com/test.integration.filters.SetResponseCodeFilterConfig
  //                     code: 403
  //   )EOF");
  //   HttpIntegrationTest::initialize();
  // }

  void buildFilterWithExtProcConfig() {
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
                    name: envoy.filters.http.ext_proc
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.http.ext_proc.v3.ExternalProcessor
                      grpc_service:
                        envoy_grpc:
                          cluster_name: "ext_proc_server_0"
                      processing_mode:
                        request_header_mode: "SEND"
                        response_header_mode: "SEND"
                        request_body_mode: "BUFFERED"
                        response_body_mode: "NONE"
                        request_trailer_mode: "SKIP"
                        response_trailer_mode: "SKIP"
    )EOF");
    //HttpIntegrationTest::initialize();
  }

///////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // Create separate "upstreams" for ExtProc gRPC servers
    for (int i = 0; i < 2; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
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
      // TODO(tyxia) Mutate config to add this to ExternalProcessor in ExecuteFilterAction
      // setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server_0",
      //                grpc_upstreams_[0]->localAddress());

      // // Construct a configuration proto for our filter and then re-write it
      // // to JSON so that we can add it to the overall config
      // envoy::config::listener::v3::Filter ext_proc_filter;
      // ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      // ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      // config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));

      // Parameterize with defer processing to prevent bit rot as filter made
      // assumptions of data flow, prior relying on eager processing.
      // config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
      //                                   deferredProcessing() ? "true" : "false");
    });
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
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

  void waitForFirstMessage(
      FakeUpstream& grpc_upstream,
      envoy::service::ext_proc::v3::ProcessingRequest& request) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_,
                                                    processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_,
                                                        processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  void handleUpstreamRequest() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(
        *dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_,
                                                            upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response,
                                int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;

};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeFilterWithExtProcIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(CompositeFilterWithExtProcIntegrationTest, GetAndCloseStream) {
  initializeConfig();
  buildFilterWithExtProcConfig();
  HttpIntegrationTest::initialize();


  //auto response = sendDownstreamRequest(absl::nullopt);

  /////////////// Solution 1 ////////
  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  const Http::TestRequestHeaderMapImpl request_headers = {{":method", "GET"},
                                                            {":path", "/somepath"},
                                                            {":scheme", "http"},
                                                            {"match-header", "match"},
                                                            {":authority", "blah"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  // ASSERT_TRUE(response->waitForEndStream());
  /////////////// Solution 1 ////////

  envoy::service::ext_proc::v3::ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Just close the stream without doing anything
  processor_stream_->startGrpcStream();
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

} // namespace Envoy
