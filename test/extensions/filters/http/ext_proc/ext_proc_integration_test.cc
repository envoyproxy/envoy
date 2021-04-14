#include <algorithm>

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
using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::HeadersResponse;
using envoy::service::ext_proc::v3alpha::HttpBody;
using envoy::service::ext_proc::v3alpha::HttpHeaders;
using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;
using Extensions::HttpFilters::ExternalProcessing::HasNoHeader;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::SingleHeaderValueIs;

using Http::LowerCaseString;

using namespace std::chrono_literals;

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

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    HttpTestUtility::addDefaultHeaders(headers);
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithBody(
      absl::string_view body,
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers, "POST");
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeRequestWithBody(headers, std::string(body));
  }

  void verifyDownstreamResponse(IntegrationStreamDecoder& response, int status_code) {
    ASSERT_TRUE(response.waitForEndStream());
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

  void handleUpstreamRequestWithResponse(const Buffer::Instance& all_data, uint64_t chunk_size) {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

    // Copy the data so that we don't modify it
    Buffer::OwnedImpl total_response = all_data;
    while (total_response.length() > 0) {
      auto to_move = std::min(total_response.length(), chunk_size);
      Buffer::OwnedImpl chunk;
      chunk.move(total_response, to_move);
      EXPECT_EQ(to_move, chunk.length());
      upstream_request_->encodeData(chunk, false);
    }
    upstream_request_->encodeData(0, true);
  }

  void waitForFirstMessage(ProcessingRequest& request) {
    ASSERT_TRUE(fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  void processRequestHeadersMessage(
      bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(
          fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_));
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
      bool first_message,
      absl::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(
          fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_));
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

  void processRequestBodyMessage(
      bool first_message, absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(
          fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_request_body());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* body = response.mutable_request_body();
    const bool sendReply = !cb || (*cb)(request.request_body(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processResponseBodyMessage(
      bool first_message, absl::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(
          fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    ASSERT_TRUE(request.has_response_body());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* body = response.mutable_response_body();
    const bool sendReply = !cb || (*cb)(request.response_body(), *body);
    if (sendReply) {
      processor_stream_->sendGrpcMessage(response);
    }
  }

  void processAndRespondImmediately(bool first_message,
                                    absl::optional<std::function<void(ImmediateResponse&)>> cb) {
    ProcessingRequest request;
    if (first_message) {
      ASSERT_TRUE(
          fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, processor_connection_));
      ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    }
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    ProcessingResponse response;
    auto* immediate = response.mutable_immediate_response();
    if (cb) {
      (*cb)(*immediate);
    }
    processor_stream_->sendGrpcMessage(response);
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
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  // Just close the stream without doing anything
  processor_stream_->startGrpcStream();
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by returning a failure before the first stream response can be sent.
TEST_P(ExtProcIntegrationTest, GetAndFailStream) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

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
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully, but then sends a gRPC error.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOutOfLineLater) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  // Fail the stream in between messages
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  verifyDownstreamResponse(*response, 500);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but closes the stream after response_headers.
TEST_P(ExtProcIntegrationTest, GetAndCloseStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
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

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// successfully but then fails on the response_headers message.
TEST_P(ExtProcIntegrationTest, GetAndFailStreamOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(request_headers_msg);
  processor_stream_->startGrpcStream();
  ProcessingResponse resp1;
  resp1.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp1);

  handleUpstreamRequest();

  ProcessingRequest response_headers_msg;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, response_headers_msg));
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

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

  processRequestHeadersMessage(true, [](const HttpHeaders& headers, HeadersResponse& headers_resp) {
    Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                            {":method", "GET"},
                                                            {"host", "host"},
                                                            {":path", "/"},
                                                            {"x-remove-this", "yes"}};
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

  processResponseHeadersMessage(false, [](const HttpHeaders& headers, HeadersResponse&) {
    Http::TestRequestHeaderMapImpl expected_response_headers{{":status", "200"}};
    EXPECT_THAT(headers.headers(), HeaderProtosEqual(expected_response_headers));
    return true;
  });

  verifyDownstreamResponse(*response, 200);
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the response_headers message
// by requesting to modify the request headers.
TEST_P(ExtProcIntegrationTest, GetAndSetHeadersOnResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
    auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
    auto* add1 = response_mutation->add_set_headers();
    add1->mutable_header()->set_key("x-response-processed");
    add1->mutable_header()->set_value("1");
    return true;
  });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter with a response body callback enabled using an
// an ext_proc server that responds to the response_body message
// by requesting to modify the response body.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyOnResponse) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
    auto* content_length =
        headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
    content_length->mutable_header()->set_key("content-length");
    content_length->mutable_header()->set_value("13");
    return true;
  });

  // Should get just one message with the body
  processResponseBodyMessage(false, [](const HttpBody& body, BodyResponse& body_resp) {
    EXPECT_TRUE(body.end_of_stream());
    auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
    body_mut->set_body("Hello, World!");
    return true;
  });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("content-length", "13"));
  EXPECT_EQ("Hello, World!", response->body());
}

// Test the filter with both body callbacks enabled and have the
// ext_proc server change both of them.
TEST_P(ExtProcIntegrationTest, GetAndSetBodyOnBoth) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);

  processRequestHeadersMessage(true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
    auto* content_length =
        headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
    content_length->mutable_header()->set_key("content-length");
    content_length->mutable_header()->set_value("13");
    return true;
  });

  processRequestBodyMessage(false, [](const HttpBody& body, BodyResponse& body_resp) {
    EXPECT_TRUE(body.end_of_stream());
    auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
    body_mut->set_body("Hello, World!");
    return true;
  });

  handleUpstreamRequest();

  processResponseHeadersMessage(false, [](const HttpHeaders&, HeadersResponse& headers_resp) {
    headers_resp.mutable_response()->mutable_header_mutation()->add_remove_headers(
        "content-length");
    return true;
  });

  processResponseBodyMessage(false, [](const HttpBody& body, BodyResponse& body_resp) {
    EXPECT_TRUE(body.end_of_stream());
    body_resp.mutable_response()->mutable_body_mutation()->set_body("123");
    return true;
  });

  verifyDownstreamResponse(*response, 200);
  EXPECT_EQ("123", response->body());
}

// Test the filter using a configuration that uses the processing mode to
// only send the response_headers message.
TEST_P(ExtProcIntegrationTest, ProcessingModeResponseOnly) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  handleUpstreamRequest();

  processResponseHeadersMessage(true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
    auto* response_mutation = headers_resp.mutable_response()->mutable_header_mutation();
    auto* add1 = response_mutation->add_set_headers();
    add1->mutable_header()->set_key("x-response-processed");
    add1->mutable_header()->set_value("1");
    return true;
  });

  verifyDownstreamResponse(*response, 200);
  EXPECT_THAT(response->headers(), SingleHeaderValueIs("x-response-processed", "1"));
}

// Test the filter using the default configuration by connecting to
// an ext_proc server that responds to the request_headers message
// by sending back an immediate_response message, which should be
// returned directly to the downstream.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediately) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);

  processAndRespondImmediately(true, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
    auto* hdr1 = immediate.mutable_headers()->add_set_headers();
    hdr1->mutable_header()->set_key("x-failure-reason");
    hdr1->mutable_header()->set_value("testing");
    auto* hdr2 = immediate.mutable_headers()->add_set_headers();
    hdr2->mutable_header()->set_key("content-type");
    hdr2->mutable_header()->set_value("application/json");
  });

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
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, absl::nullopt);
  handleUpstreamRequest();

  processAndRespondImmediately(false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with request body streaming enabled using
// an ext_proc server that responds to the request_body message
// by sending back an immediate_response message
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnRequestBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("Replace this!", absl::nullopt);
  processRequestHeadersMessage(true, absl::nullopt);
  processAndRespondImmediately(false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  verifyDownstreamResponse(*response, 401);
  EXPECT_EQ("{\"reason\": \"Not authorized\"}", response->body());
}

// Test the filter with body streaming enabled using
// an ext_proc server that responds to the response_body message
// by sending back an immediate_response message. Since this
// happens after the response headers have been sent, as a result
// Envoy should just reset the stream.
TEST_P(ExtProcIntegrationTest, GetAndRespondImmediatelyOnResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(false, absl::nullopt);

  processAndRespondImmediately(false, [](ImmediateResponse& immediate) {
    immediate.mutable_status()->set_code(envoy::type::v3::StatusCode::Unauthorized);
    immediate.set_body("{\"reason\": \"Not authorized\"}");
    immediate.set_details("Failed because you are not authorized");
  });

  // The stream should have been reset here before the complete
  // response was received.
  ASSERT_TRUE(response->waitForReset());
}

// Send a request, but wait longer than the "message timeout" before sending a response
// from the external processor. This should trigger the timeout and result
// in a 500 error.
TEST_P(ExtProcIntegrationTest, RequestMessageTimeout) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, [this](const HttpHeaders&, HeadersResponse&) {
    // Travel forward 400 ms
    timeSystem().advanceTimeWaitImpl(400ms);
    return false;
  });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 500);
}

// Same as the previous test but on the response path, since there are separate
// timers for each.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeout) {
  // ensure 200 ms timeout
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(false, [this](const HttpHeaders&, HeadersResponse&) {
    // Travel forward 400 ms
    timeSystem().advanceTimeWaitImpl(400ms);
    return false;
  });

  // We should immediately have an error response now
  verifyDownstreamResponse(*response, 500);
}

// Send a request,  wait longer than the "message timeout" before sending a response
// from the external processor, but nothing should happen because we are ignoring
// the timeout.
TEST_P(ExtProcIntegrationTest, RequestMessageTimeoutIgnoreError) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, [this](const HttpHeaders&, HeadersResponse&) {
    // Travel forward 400 ms
    timeSystem().advanceTimeWaitImpl(400ms);
    return false;
  });

  // We should be able to continue from here since the error was ignored
  handleUpstreamRequest();
  // Since we are ignoring errors, the late response to the request headers
  // message meant that subsequent messages are spurious and the response
  // headers message was never sent.
  // Despite the timeout the request should succeed.
  verifyDownstreamResponse(*response, 200);
}

// Same as the previous test but on the response path, since there are separate
// timers for each.
TEST_P(ExtProcIntegrationTest, ResponseMessageTimeoutIgnoreError) {
  proto_config_.set_failure_mode_allow(true);
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(absl::nullopt);
  processRequestHeadersMessage(true, absl::nullopt);
  handleUpstreamRequest();
  processResponseHeadersMessage(false, [this](const HttpHeaders&, HeadersResponse&) {
    // Travel forward 400 ms
    timeSystem().advanceTimeWaitImpl(400ms);
    return false;
  });

  // We should still succeed despite the timeout
  verifyDownstreamResponse(*response, 200);
}

} // namespace Envoy
