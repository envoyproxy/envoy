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

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;
using Extensions::HttpFilters::ExternalProcessing::HasNoHeader;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::SingleHeaderValueIs;

using Http::LowerCaseString;

// These tests exercise the ext_proc filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.

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
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
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
      config_helper_.addFilter(MessageUtil::getJsonStringFromMessageOrDie(ext_proc_filter));
    });
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  }

  IntegrationStreamDecoderPtr
  sendDownstreamRequest(std::function<void(Http::HeaderMap& headers)> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    if (modify_headers != nullptr) {
      modify_headers(headers);
    }
    HttpTestUtility::addDefaultHeaders(headers);
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    response.waitForEndStream();
    EXPECT_TRUE(response.complete());
    EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
  }

  void handleUpstreamRequest() {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);
  }

  void waitForFirstMessage(ProcessingRequest& request) {
    ASSERT_TRUE(fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor proto_config_{};
  FakeHttpConnectionPtr processor_connection_;
  FakeStreamPtr processor_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtProcIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by immediately closing the stream.
TEST_P(ExtProcIntegrationTest, GetAndCloseStream) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  // Just close the stream without doing anything
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  processor_stream_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by returning a failure before the first stream response can be sent.
TEST_P(ExtProcIntegrationTest, GetAndFailStream) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  // Fail the stream immediately
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);
  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully, but then sends a gRPC error.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOutOfLine) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "13"}});

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully, but then sends a gRPC error.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOutOfLineLater) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "13"}});

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but closes the stream after response_headers.
TEST_P(ExtProcIntegrationTest, GetAndCloseStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});

  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but then fails on the response_headers message.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "13"}});

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by requesting to modify the request headers.
TEST_P(ExtProcIntegrationTest, GetAndSetHeaders) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);

  ASSERT_TRUE(request_headers_msg.has_request_headers());
  const auto request_headers = request_headers_msg.request_headers();
  Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                          {":method", "GET"},
                                                          {"host", "host"},
                                                          {":path", "/"},
                                                          {"x-remove-this", "yes"}};
  EXPECT_THAT(request_headers.headers(), HeaderProtosEqual(expected_request_headers));

  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  // Ask to change the headers
  ProcessingResponse response_msg;
  auto response_header_mutation =
      response_msg.mutable_request_headers()->mutable_response()->mutable_header_mutation();
  auto mut1 = response_header_mutation->add_set_headers();
  mut1->mutable_header()->set_key("x-new-header");
  mut1->mutable_header()->set_value("new");
  response_header_mutation->add_remove_headers("x-remove-this");
  processor_stream_->sendGrpcMessage(response_msg);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(), HasNoHeader("x-remove-this"));
  EXPECT_THAT(upstream_request_->headers(), SingleHeaderValueIs("x-new-header", "new"));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Now expect a message for the response path
  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  ASSERT_TRUE(response_headers_msg.has_response_headers());
  const auto response_headers = response_headers_msg.response_headers();
  Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
  EXPECT_THAT(response_headers.headers(), HeaderProtosEqual(expected_response_headers));

  // Send back a response but don't do anything
  ProcessingResponse response_2;
  response_2.mutable_response_headers();
  processor_stream_->sendGrpcMessage(response_2);

  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// by requesting to modify the request headers.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);

  ASSERT_TRUE(request_headers_msg.has_request_headers());
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  ASSERT_TRUE(response_headers_msg.has_response_headers());
  ProcessingResponse resp2;
  auto* headers2 = resp2.mutable_response_headers();
  auto* response_mutation = headers2->mutable_response()->mutable_header_mutation();
  auto* add1 = response_mutation->add_set_headers();
  add1->mutable_header()->set_key("x-response-processed");
  add1->mutable_header()->set_value("1");
  processor_stream_->sendGrpcMessage(resp2);

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using a configuration that uses the processing mode to
// only send the response_headers message.
TEST_P(ExtProcIntegrationTest, ProcessingModeResponseOnly) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);
  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  waitForFirstMessage(response_headers_msg);
  ASSERT_TRUE(response_headers_msg.has_response_headers());
  ProcessingResponse resp2;
  auto* headers2 = resp2.mutable_response_headers();
  auto* response_mutation = headers2->mutable_response()->mutable_header_mutation();
  auto* add1 = response_mutation->add_set_headers();
  add1->mutable_header()->set_key("x-response-processed");
  add1->mutable_header()->set_value("1");
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  processor_stream_->sendGrpcMessage(resp2);

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by sending back an immediate_response message, which should be
// returned directly to the downstream.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediately) {
  // Logger::Registry::getLog(Logger::Id::filter).set_level(spdlog::level::trace);

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);

  EXPECT_TRUE(request_headers_msg.has_request_headers());
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  // Produce an immediate response
  ProcessingResponse response_msg;
  auto* immediate_response = response_msg.mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
  immediate_response->set_body("{\"reason\": \"Not authorized\"}");
  immediate_response->set_details("Failed because you are not authorized");
  auto* hdr1 = immediate_response->mutable_headers()->add_set_headers();
  hdr1->mutable_header()->set_key("x-failure-reason");
  hdr1->mutable_header()->set_value("testing");
  auto* hdr2 = immediate_response->mutable_headers()->add_set_headers();
  hdr2->mutable_header()->set_key("content-type");
  hdr2->mutable_header()->set_value("application/json");
  processor_stream_->sendGrpcMessage(response_msg);

  verifyDownstreamResponse(*response, 401);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-failure-reason", "testing"));
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("content-type", "application/json"));
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by sending back an immediate_response message after the
// request_headers message
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(nullptr);

  // request_headers message to processor
  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  EXPECT_TRUE(request_headers_msg.has_request_headers());
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  // Response to request_headers
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  // response_headers message to processor
  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  ASSERT_TRUE(response_headers_msg.has_response_headers());

  // Response to response_headers
  ProcessingResponse resp2;
  auto* immediate_response = resp2.mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
  immediate_response->set_body("{\"reason\": \"Not authorized\"}");
  immediate_response->set_details("Failed because you are not authorized");
  processor_stream_->sendGrpcMessage(resp2);

  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

} // namespace Envoy