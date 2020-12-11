#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

#include "extensions/filters/http/ext_proc/config.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Extensions::HttpFilters::ExternalProcessing::expectHttpHeader;
using Http::LowerCaseString;

class ExtProcIntegrationTest : public HttpIntegrationTest,
                               public Grpc::GrpcClientIntegrationParamTest {
protected:
  ExtProcIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    // Need to create a separate "upstream" for the gRPC server
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
  }

  void TearDown() override {
    if (processor_connection_) {
      AssertionResult result = processor_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = processor_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // This is the cluster for our gRPC server, starting by copying an existing cluster
      auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
      server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      server_cluster->set_name("ext_proc_server");
      server_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");
      ConfigHelper::setHttp2(*server_cluster);

      // Load configuration of the server from YAML and use a helper to add a grpc_service
      // stanza pointing to the cluster that we just made
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server",
                     fake_upstreams_.back()->localAddress());

      // Construct a configuration proto for our filter and then re-write it
      // to JSON so that we can add it to the overall config
      envoy::config::listener::v3::Filter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.addFilter(MessageUtil::getJsonStringFromMessage(ext_proc_filter));
      std::cerr << MessageUtil::getJsonStringFromMessage(bootstrap, true) << '\n';
    });
  }

  envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor proto_config_{};
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtProcIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ExtProcIntegrationTest, GetAndCloseStream) {
  initializeConfig();
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto response = codec_client_->makeHeaderOnlyRequest(headers);

  // Expect a message on the gRPC stream to the fake remote service
  AssertionResult result =
      fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_);
  RELEASE_ASSERT(result, result.message());
  result = processor_connection_->waitForNewStream(*dispatcher_, processor_stream_);
  RELEASE_ASSERT(result, result.message());
  ProcessingRequest request_headers_msg;
  result = processor_stream_->waitForGrpcMessage(*dispatcher_, request_headers_msg);
  RELEASE_ASSERT(result, result.message());
  // Just close the stream without doing anything
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  processor_stream_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});

  // Now expect a message to the real upstream
  result = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  result = upstream_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  // Respond from the upstream with a simple 200
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Now expect a response to the original request
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ExtProcIntegrationTest, GetAndFailStream) {
  initializeConfig();
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto response = codec_client_->makeHeaderOnlyRequest(headers);

  AssertionResult result =
      fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_);
  RELEASE_ASSERT(result, result.message());
  result = processor_connection_->waitForNewStream(*dispatcher_, processor_stream_);
  RELEASE_ASSERT(result, result.message());
  ProcessingRequest request_headers_msg;
  result = processor_stream_->waitForGrpcMessage(*dispatcher_, request_headers_msg);
  RELEASE_ASSERT(result, result.message());
  // Fail the stream immediately
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

TEST_P(ExtProcIntegrationTest, GetAndSetHeaders) {
  initializeConfig();
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  HttpIntegrationTest::initialize();

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy(LowerCaseString("x-remove-this"), "yes");
  auto response = codec_client_->makeHeaderOnlyRequest(headers);

  AssertionResult result =
      fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_);
  RELEASE_ASSERT(result, result.message());
  result = processor_connection_->waitForNewStream(*dispatcher_, processor_stream_);
  RELEASE_ASSERT(result, result.message());
  ProcessingRequest request_headers_msg;
  result = processor_stream_->waitForGrpcMessage(*dispatcher_, request_headers_msg);
  RELEASE_ASSERT(result, result.message());

  EXPECT_TRUE(request_headers_msg.has_request_headers());
  const auto request_headers = request_headers_msg.request_headers();
  expectHttpHeader(request_headers.headers(), ":method", "GET");
  expectHttpHeader(request_headers.headers(), ":path", "/");
  expectHttpHeader(request_headers.headers(), "x-remove-this", "yes");

  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  // Ask to change the headers
  ProcessingResponse response_msg;
  auto response_headers_msg = response_msg.mutable_request_headers();
  auto response_header_mutation =
      response_headers_msg->mutable_response()->mutable_header_mutation();
  auto mut1 = response_header_mutation->add_set_headers();
  mut1->mutable_header()->set_key("x-new-header");
  mut1->mutable_header()->set_value("new");
  response_header_mutation->add_remove_headers("x-remove-this");
  processor_stream_->sendGrpcMessage(response_msg);

  result = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  result = upstream_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  auto has_hdr1 = upstream_request_->headers().get(LowerCaseString("x-remove-this"));
  EXPECT_TRUE(has_hdr1.empty());
  auto has_hdr2 = upstream_request_->headers().get(LowerCaseString("x-new-header"));
  EXPECT_EQ(has_hdr2.size(), 1);
  EXPECT_EQ(has_hdr2[0]->value(), "new");

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace Envoy