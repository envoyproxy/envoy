// Changing the default behavior of ext_proc is generally not allowed. While you may add tests, you
// generally should not change or remove existing tests.

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"
#include "test/integration/http_integration.h"

// Tests for status_on_error functionality.
namespace Envoy {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

class ExtProcStatusOnErrorIntegrationTest : public HttpIntegrationTest,
                                            public Grpc::GrpcClientIntegrationParamTest {
protected:
  ExtProcStatusOnErrorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate "upstreams" for ExtProc gRPC servers
    for (int i = 0; i < grpc_upstream_count_; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void initializeConfig(uint32_t status_code) {
    config_helper_.addConfigModifier([this, status_code](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
      server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      server_cluster->set_name("ext_proc_server");
      server_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");

      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server",
                     grpc_upstreams_[0]->localAddress());

      proto_config_.mutable_status_on_error()->set_code(
          static_cast<envoy::type::v3::StatusCode>(status_code));
      proto_config_.set_failure_mode_allow(false);

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      std::ignore = ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
    });

    setUpstreamProtocol(Http::CodecType::HTTP1);
    setDownstreamProtocol(Http::CodecType::HTTP1);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      std::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  void waitForFirstMessage(FakeUpstream& grpc_upstream, ProcessingRequest& request) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  bool isEnvoyGrpc() { return std::get<1>(GetParam()) == Envoy::Grpc::ClientType::EnvoyGrpc; }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_;
  FakeStreamPtr processor_stream_;
  FakeHttpConnectionPtr processor_connection_;
  std::vector<FakeUpstream*> grpc_upstreams_;
  int grpc_upstream_count_ = 1;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtProcStatusOnErrorIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Test that status_on_error is used when gRPC stream encounters an error.
TEST_P(ExtProcStatusOnErrorIntegrationTest, GrpcStreamErrorCustomStatus) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  initializeConfig(503); // Use 503 Service Unavailable.
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(std::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  // Send successful response first, then simulate gRPC stream error.
  ProcessingResponse resp;
  resp.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp);

  // Now trigger gRPC stream error which should use status_on_error.
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Should get custom status code 503 instead of default 500.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Test that default status (500) is used when status_on_error is not configured.
TEST_P(ExtProcStatusOnErrorIntegrationTest, GrpcStreamErrorDefaultStatus) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  // Initialize without setting status_on_error, should default to 500.
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
    server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    server_cluster->set_name("ext_proc_server");
    server_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");

    setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server",
                   grpc_upstreams_[0]->localAddress());

    proto_config_.set_failure_mode_allow(false);

    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_proc_filter;
    ext_proc_filter.set_name("envoy.filters.http.ext_proc");
    std::ignore = ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
  });

  setUpstreamProtocol(Http::CodecType::HTTP1);
  setDownstreamProtocol(Http::CodecType::HTTP1);

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(std::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  // Send successful response first, then simulate gRPC stream error.
  ProcessingResponse resp;
  resp.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp);

  // Trigger gRPC stream error without status_on_error configured.
  processor_stream_->finishGrpcStream(Grpc::Status::Internal);

  // Should get default status code 500.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Test that message timeout returns 504 Gateway Timeout regardless of status_on_error.
TEST_P(ExtProcStatusOnErrorIntegrationTest, MessageTimeoutReturnsGatewayTimeout) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  initializeConfig(502); // Configure 502, but timeout should return 504.
  proto_config_.mutable_message_timeout()->set_nanos(100000000); // 100ms timeout.

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(std::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  // Don't send a response to trigger timeout - just wait for the timeout to occur.

  // Let timeout occur.
  test_server_->waitForCounter("http.config_test.ext_proc.message_timeouts", testing::Ge(1));

  // Should return 504 Gateway Timeout instead of configured status_on_error.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("504", response->headers().getStatusValue());
}

// Test that status_on_error is used when processing/mutation errors occur.
TEST_P(ExtProcStatusOnErrorIntegrationTest, ProcessingErrorCustomStatus) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::EnvoyGrpc);

  initializeConfig(422); // Use 422 Unprocessable Entity.
  // Enable strict mutation rules to trigger processing errors.
  proto_config_.mutable_mutation_rules()->mutable_disallow_is_error()->set_value(true);
  proto_config_.mutable_mutation_rules()->mutable_disallow_system()->set_value(true);

  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(std::nullopt);

  // Process the request and send back invalid system header mutation.
  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  processor_stream_->startGrpcStream();

  ProcessingResponse resp;
  auto* header_mut = resp.mutable_request_headers()->mutable_response()->mutable_header_mutation();
  auto* header = header_mut->add_set_headers();
  header->mutable_append()->set_value(false);
  header->mutable_header()->set_key(":system-header"); // This should trigger error.
  header->mutable_header()->set_raw_value("invalid");
  processor_stream_->sendGrpcMessage(resp);

  // Should get custom status code 422 for processing error.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("422", response->headers().getStatusValue());
}

class ExtProcGracefulCloseIntegrationTest
    : public Extensions::HttpFilters::ExternalProcessing::ExtProcIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeferredProcessing,
                         ExtProcGracefulCloseIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ExtProcGracefulCloseIntegrationTest, ServerEndStreamOnLastExpectedReply) {
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.ext_proc_graceful_grpc_close", "true"}});
  // Envoy gRPC only: the Google gRPC client uses a different completion path.
  if (clientType() != Grpc::ClientType::EnvoyGrpc) {
    GTEST_SKIP();
  }
  // Only request_headers are sent so the request_headers reply is "last".+
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequest(std::nullopt);
  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  EXPECT_TRUE(request_headers_msg.has_request_headers());
  processor_stream_->startGrpcStream();

  // Send the request_headers reply as gRPC-framed DATA with END_STREAM set
  // and no trailers. This is not gRPC spec compliant, the test just checks
  // that this path does not crash.
  ProcessingResponse resp;
  resp.mutable_request_headers();
  auto serialized = Grpc::Common::serializeToGrpcFrame(resp);
  processor_stream_->encodeData(*serialized, /*end_stream=*/true);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcGracefulCloseIntegrationTest, GracefulHalfCloseThenLateNon200Headers) {
  // The graceful-close path only exists for the EnvoyGrpc client (it goes
  // through Grpc::AsyncStreamImpl, not GoogleAsyncStreamImpl).
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP();
  }

  // Enable the graceful-close path so onDestroy takes
  // halfCloseAndWaitForRemoteClose() instead of closeStream()/resetStream().
  config_helper_.addRuntimeOverride("envoy.reloadable_features.ext_proc_graceful_grpc_close",
                                    "true");

  // Minimal processing mode: just request and response headers, so that ext_proc
  // stream is not closed early on request header response.
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);

  initializeConfig();
  HttpIntegrationTest::initialize();

  // 1. Downstream → Envoy: header-only GET. ext_proc opens the gRPC stream and
  //    sends the request_headers ProcessingRequest.
  auto response = sendDownstreamRequest(std::nullopt);

  // Wait for the gRPC side-stream to be fully established and for Envoy's
  // ProcessingRequest to arrive. We deliberately do NOT call
  // startGrpcStream() — the fake server has not yet sent its response
  // HEADERS (:status:200), so AsyncStreamImpl::onHeaders has not yet fired.
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ProcessingRequest req;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, req));
  ASSERT_TRUE(req.has_request_headers());

  // 2. Close downstream connection to cause ext_proc filter to be deleted
  //    and the AsyncGrpcClient to enter graceful close.
  codec_client_->close();
  ASSERT_TRUE(response->waitForReset());

  // Wait for graceful half-close to show up at the ext_proc server.
  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));

  // 3. ext_proc server now sends its first response headers as a
  //    non-200 trailers-only response. Make sure callbacks in the
  //    deleted ext_proc are not called.
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}},
                                   /*end_stream=*/false);

  // Give the worker dispatcher a chance to process the headers.
  ASSERT_TRUE(processor_stream_->waitForReset());
}

TEST_P(ExtProcGracefulCloseIntegrationTest, StreamErrorOrphansHttpStream) {
  // The graceful-close path only exists for the EnvoyGrpc client (it goes
  // through Grpc::AsyncStreamImpl, not GoogleAsyncStreamImpl).
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP();
  }
  // Enable the graceful-close path so that onDestroy goes via
  // halfCloseAndWaitForRemoteClose() instead of closeStream()+resetStream().
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.ext_proc_graceful_grpc_close", "true"}});

  // Only process request headers; everything else SKIP so the downstream
  // request can complete without further sidecar round-trips.
  proto_config_.mutable_processing_mode()->set_request_header_mode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);

  initializeConfig();
  HttpIntegrationTest::initialize();

  // 1. Downstream request opens ext_proc side stream.
  auto response = sendDownstreamRequest(std::nullopt);

  // 2. ext_proc server accepts the gRPC stream, sends 200 headers (startGrpcStream),
  //    and replies to the request_headers message so the downstream chain
  //    continues to the upstream.
  processRequestHeadersMessage(*grpc_upstreams_[0], /*first_message=*/true, std::nullopt);

  // 3. Finish the request to cause ext_proc filter to be deleted.
  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);

  // 4. Wait until the gRPC side-stream has been half-closed by Envoy
  //    (i.e. ext_proc::onDestroy has run on the worker).
  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));

  // 5. ext_proc server sends a DATA frame (NOT end_stream) carrying a malformed gRPC
  //    frame. This cases GrpcAsyncStream to be reset and scheduled for deferred deletion.
  {
    Buffer::OwnedImpl bad_flag("\x02", 1);
    processor_stream_->encodeData(bad_flag, /*end_stream=*/false);
  }

  // 6. The deferredDelete callback is scheduled in the worker dispatcher's
  //    current iteration. Once that iteration finishes, ~Grpc::AsyncStreamImpl
  //    fires and frees the object (and its headers_message_). Send a series of
  //    follow-up DATA frames with small gaps so that at least one of them is
  //    dispatched in a SEPARATE worker I/O event after the deferred-delete has
  //    run.
  for (int i = 0; i < 20; ++i) {
    timeSystem().advanceTimeWait(std::chrono::milliseconds(5));
    Buffer::OwnedImpl more("\x00\x00\x00\x00\x00", 5); // benign 0-len gRPC frame
    processor_stream_->encodeData(more, /*end_stream=*/false);
  }

  // Give the worker time to dispatch the last frames.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // Make sure we got here without ASAN tripping,
  processor_stream_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}});
  test_server_->waitForCounter("cluster.ext_proc_server_0.upstream_rq_completed", ::testing::Ge(1));
}

TEST_P(ExtProcGracefulCloseIntegrationTest, GracefulCloseWithErrorResponse) {
  // The graceful-close path only exists for the EnvoyGrpc client (it goes
  // through Grpc::AsyncStreamImpl, not GoogleAsyncStreamImpl).
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP();
  }
  config_helper_.addRuntimeOverride("envoy.reloadable_features.ext_proc_graceful_grpc_close",
                                    "true");

  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();

  // (1) Downstream → Envoy: start a request (no end-stream so we can reset it).
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Accept ext_proc request but do not respond.
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ProcessingRequest req;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, req));
  ASSERT_TRUE(req.has_request_headers());

  // Reset downstream request that causes ext_proc filter to be deleted. This also
  // deletes ProcessorStreamImpl which has callbacks from AsyncGrpcClient
  codec_client_->sendReset(*request_encoder_);
  ASSERT_TRUE(response->waitForReset());

  // half closing AsyncGrpcClient writes a 0-length DATA(end_stream).
  // Wait for it so we know the ext_proc was freed.
  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));

  // Send a non-200 response from the ext_proc server, to check that it does
  // no call into deleted ProcessorStreamImpl
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}},
                                   /*end_stream=*/true);

  // Synchronize: wait for the worker thread to dispatch the H2 HEADERS frame.
  // Without the fix ASAN aborts inside Grpc::AsyncStreamImpl::onHeaders before this returns.
  test_server_->waitForCounter("cluster.ext_proc_server_0.upstream_rq_503", ::testing::Ge(1));
  codec_client_->close();
}

} // namespace Envoy
