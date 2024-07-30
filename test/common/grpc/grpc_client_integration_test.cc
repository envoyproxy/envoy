#ifdef ENVOY_GOOGLE_GRPC
#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/google_async_client_impl.h"

#endif

#include "test/test_common/test_runtime.h"
#include "test/common/grpc/grpc_client_integration_test_harness.h"

using testing::Eq;

namespace Envoy {
namespace Grpc {
namespace {

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, EnvoyGrpcFlowControlTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         EnvoyGrpcClientIntegrationParamTest::protocolTestParamsToString);

TEST_P(EnvoyGrpcFlowControlTest, BasicStreamWithFlowControl) {
  // Configure the connection buffer limit to 1KB
  connection_buffer_limits_ = 1024;
  // Create large request string that will trigger watermark given buffer limit above.
  std::string large_request = std::string(64 * 1024, 'a');

  initialize();
  auto stream = createStream(empty_metadata_);

  testing::StrictMock<Http::MockSidestreamWatermarkCallbacks> watermark_callbacks;

  // Registering the new watermark callback.
  stream->grpc_stream_->setWatermarkCallbacks(watermark_callbacks);
  // Expect that flow control kicks in and watermark calls are triggered.
  EXPECT_CALL(watermark_callbacks, onSidestreamAboveHighWatermark());
  EXPECT_CALL(watermark_callbacks, onSidestreamBelowLowWatermark());

  // Create send request with large request string.
  helloworld::HelloRequest request_msg;
  request_msg.set_name(large_request);

  RequestArgs request_args;
  request_args.request = &request_msg;
  stream->sendRequest(request_args);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Parameterize the loopback test server socket address and gRPC client type.
INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, GrpcClientIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Validate that a simple request-reply stream works.
TEST_P(GrpcClientIntegrationTest, BasicStream) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// A simple request-reply stream, "x-envoy-internal" and `x-forward-for` headers
// are removed due to grpc service configuration.
TEST_P(GrpcClientIntegrationTest, BasicStreamRemoveInternalHeaders) {
  // "x-envoy-internal" and `x-forward-for` headers are only available on Envoy gRPC path.
  SKIP_IF_GRPC_CLIENT(ClientType::GoogleGrpc);
  skip_envoy_headers_ = true;
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// A simple request-reply stream, "x-envoy-internal" and `x-forward-for` headers
// are removed due to per stream options which overrides the gRPC service configuration.
TEST_P(GrpcClientIntegrationTest, BasicStreamRemoveInternalHeadersWithStreamOption) {
  // "x-envoy-internal" and `x-forward-for` headers are only available on Envoy gRPC path.
  SKIP_IF_GRPC_CLIENT(ClientType::GoogleGrpc);
  send_internal_header_stream_option_ = false;
  send_xff_header_stream_option_ = false;
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that a simple request-reply stream works with bytes metering in Envoy gRPC.
TEST_P(GrpcClientIntegrationTest, BasicStreamWithBytesMeter) {
  // Currently, only Envoy gRPC's bytes metering is based on the bytes meter in Envoy.
  // Therefore, skip the test for google gRPC.
  SKIP_IF_GRPC_CLIENT(ClientType::GoogleGrpc);
  // The check in this test is based on HTTP2 codec logic (i.e., including H2_FRAME_HEADER_SIZE).
  // Skip this test if default protocol of this integration test is no longer HTTP2.
  if (fake_upstream_config_.upstream_protocol_ != Http::CodecType::HTTP2) {
    return;
  }
  initialize();
  auto stream = createStream(empty_metadata_);

  // Create the send request.
  helloworld::HelloRequest request_msg;
  request_msg.set_name(HELLO_REQUEST);

  RequestArgs request_args;
  request_args.request = &request_msg;
  stream->sendRequest(request_args);
  stream->sendServerInitialMetadata(empty_metadata_);

  auto send_buf = Common::serializeMessage(request_msg);
  Common::prependGrpcFrameHeader(*send_buf);

  auto upstream_meter = stream->grpc_stream_->streamInfo().getUpstreamBytesMeter();
  uint64_t total_bytes_sent = upstream_meter->wireBytesSent();
  uint64_t header_bytes_sent = upstream_meter->headerBytesSent();
  // Verify the number of sent bytes that is tracked in stream info equals to the length of
  // request buffer.
  // Note, in HTTP2 codec, H2_FRAME_HEADER_SIZE is always included in bytes meter so we need to
  // account for it in the check here as well.
  EXPECT_EQ(total_bytes_sent - header_bytes_sent,
            send_buf->length() + Http::Http2::H2_FRAME_HEADER_SIZE);

  stream->sendReply(/*check_response_size=*/true);
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
}

// Validate that a client destruction with open streams cleans up appropriately.
TEST_P(GrpcClientIntegrationTest, ClientDestruct) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  grpc_client_.reset();
}

// Validate that a simple request-reply unary RPC works.
TEST_P(GrpcClientIntegrationTest, BasicRequest) {
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that multiple streams work.
TEST_P(GrpcClientIntegrationTest, MultiStream) {
  initialize();
  auto stream_0 = createStream(empty_metadata_);
  auto stream_1 = createStream(empty_metadata_);
  stream_0->sendRequest();
  stream_1->sendRequest();
  // Access stream info to make sure it is present.
  if (std::get<1>(GetParam()) == ClientType::EnvoyGrpc) {
    envoy::config::core::v3::Metadata m;
    (*m.mutable_filter_metadata())["com.foo.bar"] = {};
    EXPECT_THAT(stream_0->grpc_stream_.streamInfo().dynamicMetadata(), ProtoEq(m));
    EXPECT_THAT(stream_1->grpc_stream_.streamInfo().dynamicMetadata(), ProtoEq(m));
  }
  stream_0->sendServerInitialMetadata(empty_metadata_);
  stream_0->sendReply();
  stream_1->sendServerTrailers(Status::WellKnownGrpcStatus::Unavailable, "", empty_metadata_, true);
  stream_0->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that multiple streams work with bytes metering in Envoy gRPC.
TEST_P(GrpcClientIntegrationTest, MultiStreamWithBytesMeter) {
  SKIP_IF_GRPC_CLIENT(ClientType::GoogleGrpc);
  // The check in this test is based on HTTP2 codec logic (i.e., including H2_FRAME_HEADER_SIZE).
  // Skip this test if default protocol of this integration test is no longer HTTP2.
  if (fake_upstream_config_.upstream_protocol_ != Http::CodecType::HTTP2) {
    return;
  }
  initialize();
  auto stream_0 = createStream(empty_metadata_);
  auto stream_1 = createStream(empty_metadata_);
  // Create the send request.
  helloworld::HelloRequest request_msg;
  request_msg.set_name(HELLO_REQUEST);

  RequestArgs request_args;
  request_args.request = &request_msg;
  stream_0->sendRequest(request_args);
  stream_1->sendRequest(request_args);

  auto send_buf = Common::serializeMessage(request_msg);
  Common::prependGrpcFrameHeader(*send_buf);

  // Access stream info to make sure it is present.
  envoy::config::core::v3::Metadata m;
  (*m.mutable_filter_metadata())["com.foo.bar"] = {};
  EXPECT_THAT(stream_0->grpc_stream_.streamInfo().dynamicMetadata(), ProtoEq(m));
  EXPECT_THAT(stream_1->grpc_stream_.streamInfo().dynamicMetadata(), ProtoEq(m));

  auto upstream_meter_0 = stream_0->grpc_stream_->streamInfo().getUpstreamBytesMeter();
  uint64_t total_bytes_sent = upstream_meter_0->wireBytesSent();
  uint64_t header_bytes_sent = upstream_meter_0->headerBytesSent();
  EXPECT_EQ(total_bytes_sent - header_bytes_sent,
            send_buf->length() + Http::Http2::H2_FRAME_HEADER_SIZE);

  auto upstream_meter_1 = stream_1->grpc_stream_->streamInfo().getUpstreamBytesMeter();
  uint64_t total_bytes_sent_1 = upstream_meter_1->wireBytesSent();
  uint64_t header_bytes_sent_1 = upstream_meter_1->headerBytesSent();
  EXPECT_EQ(total_bytes_sent_1 - header_bytes_sent_1,
            send_buf->length() + Http::Http2::H2_FRAME_HEADER_SIZE);

  stream_0->sendServerInitialMetadata(empty_metadata_);
  stream_0->sendReply(true);
  stream_1->sendServerTrailers(Status::WellKnownGrpcStatus::Unavailable, "", empty_metadata_, true);
  stream_0->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that multiple request-reply unary RPCs works.
TEST_P(GrpcClientIntegrationTest, MultiRequest) {
  initialize();
  auto request_0 = createRequest(empty_metadata_);
  auto request_1 = createRequest(empty_metadata_);
  request_1->sendReply();
  request_0->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that a non-200 HTTP status results in the expected gRPC error.
TEST_P(GrpcClientIntegrationTest, HttpNon200Status) {
  initialize();
  for (const auto http_response_status : {400, 401, 403, 404, 429, 431}) {
    auto stream = createStream(empty_metadata_);
    const Http::TestResponseHeaderMapImpl reply_headers{
        {":status", std::to_string(http_response_status)}};
    stream->expectInitialMetadata(empty_metadata_);
    stream->expectTrailingMetadata(empty_metadata_);
    // Translate status per
    // // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md
    stream->expectGrpcStatus(Utility::httpToGrpcStatus(http_response_status));
    stream->fake_stream_->encodeHeaders(reply_headers, true);
    dispatcher_helper_.runDispatcher();
  }
}

// Validate that a non-200 HTTP status results in fallback to grpc-status.
TEST_P(GrpcClientIntegrationTest, GrpcStatusFallback) {
  initialize();
  auto stream = createStream(empty_metadata_);
  const Http::TestResponseHeaderMapImpl reply_headers{
      {":status", "404"},
      {"grpc-status", std::to_string(enumToInt(Status::WellKnownGrpcStatus::PermissionDenied))},
      {"grpc-message", "error message"}};
  stream->expectInitialMetadata(empty_metadata_);
  stream->expectTrailingMetadata(empty_metadata_);
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::PermissionDenied);
  stream->fake_stream_->encodeHeaders(reply_headers, true);
  dispatcher_helper_.runDispatcher();
}

// Validate that a HTTP-level reset is handled as an INTERNAL gRPC error.
TEST_P(GrpcClientIntegrationTest, HttpReset) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendServerInitialMetadata(empty_metadata_);
  dispatcher_helper_.runDispatcher();
  stream->expectTrailingMetadata(empty_metadata_);
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::Internal);
  stream->fake_stream_->encodeResetStream();
  dispatcher_helper_.runDispatcher();
}

// Validate that a reply with bad gRPC framing (compressed frames with Envoy
// client) is handled as an INTERNAL gRPC error.
TEST_P(GrpcClientIntegrationTest, BadReplyGrpcFraming) {
  initialize();
  // Only testing behavior of Envoy client, since Google client handles
  // compressed frames.
  SKIP_IF_GRPC_CLIENT(ClientType::GoogleGrpc);
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->expectTrailingMetadata(empty_metadata_);
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::Internal);
  Buffer::OwnedImpl reply_buffer("\xde\xad\xbe\xef\x00", 5);
  stream->fake_stream_->encodeData(reply_buffer, true);
  dispatcher_helper_.runDispatcher();
}

// Validate that a reply that exceeds gRPC maximum frame size is handled as an RESOURCE_EXHAUSTED
// gRPC error.
TEST_P(GrpcClientIntegrationTest, BadReplyOverGrpcFrameLimit) {
  // Only testing behavior of Envoy client, since `max_receive_message_length` configuration is
  // added to Envoy-gRPC only.
  SKIP_IF_GRPC_CLIENT(ClientType::GoogleGrpc);

  helloworld::HelloReply reply;
  reply.set_message("HelloWorld");

  initialize(/*envoy_grpc_max_recv_msg_length=*/2);

  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->expectTrailingMetadata(empty_metadata_);
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::ResourceExhausted);
  auto serialized_response = Grpc::Common::serializeToGrpcFrame(reply);
  stream->fake_stream_->encodeData(*serialized_response, true);
  dispatcher_helper_.runDispatcher();
}

// Validate that custom channel args can be set on the Google gRPC client.
//
TEST_P(GrpcClientIntegrationTest, CustomChannelArgs) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  channel_args_.emplace_back("grpc.primary_user_agent", "test_agent");
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
  EXPECT_THAT(stream_headers_->get_("user-agent"), testing::HasSubstr("test_agent"));
}

// Validate that a reply with bad protobuf is handled as an INTERNAL gRPC error.
TEST_P(GrpcClientIntegrationTest, BadReplyProtobuf) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->expectTrailingMetadata(empty_metadata_);
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::Internal);
  Buffer::OwnedImpl reply_buffer("\x00\x00\x00\x00\x02\xff\xff", 7);
  stream->fake_stream_->encodeData(reply_buffer, true);
  dispatcher_helper_.runDispatcher();
}

// Validate that a reply with bad protobuf is handled as an INTERNAL gRPC error.
TEST_P(GrpcClientIntegrationTest, BadRequestReplyProtobuf) {
  initialize();
  auto request = createRequest(empty_metadata_);
  request->fake_stream_->startGrpcStream();
  EXPECT_CALL(*request->child_span_, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("0")));
  EXPECT_CALL(*request, onFailure(Status::Internal, "", _)).WillExitIfNeeded();
  EXPECT_CALL(*request->child_span_, finishSpan());
  dispatcher_helper_.setStreamEventPending();
  Buffer::OwnedImpl reply_buffer("\x00\x00\x00\x00\x02\xff\xff", 7);
  Common::prependGrpcFrameHeader(reply_buffer);
  request->fake_stream_->encodeData(reply_buffer, false);
  request->fake_stream_->finishGrpcStream(Grpc::Status::Ok);
  dispatcher_helper_.runDispatcher();
}

// Validate that an out-of-range gRPC status is handled as an INVALID_CODE gRPC
// error.
TEST_P(GrpcClientIntegrationTest, OutOfRangeGrpcStatus) {
  initialize();
  // TODO(htuch): there is an UBSAN issue with Google gRPC client library
  // handling of out-of-range status codes, see
  // https://circleci.com/gh/envoyproxy/envoy/20234?utm_campaign=vcs-integration-link&utm_medium=referral&utm_source=github-build-link
  // Need to fix this issue upstream first.
  SKIP_IF_GRPC_CLIENT(ClientType::GoogleGrpc);
  auto stream = createStream(empty_metadata_);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  EXPECT_CALL(*stream, onReceiveTrailingMetadata_(_)).WillExitIfNeeded();
  dispatcher_helper_.setStreamEventPending();
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::InvalidCode);
  const Http::TestResponseTrailerMapImpl reply_trailers{{"grpc-status", std::to_string(0x1337)}};
  stream->fake_stream_->encodeTrailers(reply_trailers);
  dispatcher_helper_.runDispatcher();
}

// Validate that a missing gRPC status is handled as an UNKNOWN gRPC error.
TEST_P(GrpcClientIntegrationTest, MissingGrpcStatus) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  EXPECT_CALL(*stream, onReceiveTrailingMetadata_(_)).WillExitIfNeeded();
  dispatcher_helper_.setStreamEventPending();
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::Unknown);
  const Http::TestResponseTrailerMapImpl reply_trailers{{"some", "other header"}};
  stream->fake_stream_->encodeTrailers(reply_trailers);
  dispatcher_helper_.runDispatcher();
}

// Validate that a reply terminated without trailers is handled as a gRPC error.
TEST_P(GrpcClientIntegrationTest, ReplyNoTrailers) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  helloworld::HelloReply reply;
  reply.set_message(HELLO_REPLY);
  EXPECT_CALL(*stream, onReceiveMessage_(HelloworldReplyEq(HELLO_REPLY))).WillExitIfNeeded();
  dispatcher_helper_.setStreamEventPending();
  stream->expectTrailingMetadata(empty_metadata_);
  stream->expectGrpcStatus(Status::WellKnownGrpcStatus::InvalidCode);
  auto serialized_response = Grpc::Common::serializeToGrpcFrame(reply);
  stream->fake_stream_->encodeData(*serialized_response, true);
  stream->fake_stream_->encodeResetStream();
  dispatcher_helper_.runDispatcher();
}

// Validate that sending client initial metadata works.
TEST_P(GrpcClientIntegrationTest, StreamClientInitialMetadata) {
  initialize();
  const TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
  };
  auto stream = createStream(initial_metadata);
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_, true);
  dispatcher_helper_.runDispatcher();
}

// Validate that sending client initial metadata works.
TEST_P(GrpcClientIntegrationTest, RequestClientInitialMetadata) {
  initialize();
  const TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
  };
  auto request = createRequest(initial_metadata);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that setting service-wide client initial metadata works.
TEST_P(GrpcClientIntegrationTest, RequestServiceWideInitialMetadata) {
  service_wide_initial_metadata_.emplace_back(Http::LowerCaseString("foo"), "bar");
  service_wide_initial_metadata_.emplace_back(Http::LowerCaseString("baz"), "blah");
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that receiving server initial metadata works.
TEST_P(GrpcClientIntegrationTest, ServerInitialMetadata) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  const TestMetadata initial_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
      {Http::LowerCaseString("binary-bin"), "help"},
  };
  stream->sendServerInitialMetadata(initial_metadata);
  stream->sendReply();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that receiving server trailing metadata works.
TEST_P(GrpcClientIntegrationTest, ServerTrailingMetadata) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendRequest();
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  const TestMetadata trailing_metadata = {
      {Http::LowerCaseString("foo"), "bar"},
      {Http::LowerCaseString("baz"), "blah"},
  };
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", trailing_metadata);
  dispatcher_helper_.runDispatcher();
}

// Validate that a trailers-only response is handled for streams.
TEST_P(GrpcClientIntegrationTest, StreamTrailersOnly) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_, true);
  dispatcher_helper_.runDispatcher();
}

// Validate that a trailers-only response is handled for requests, where it is
// an error.
TEST_P(GrpcClientIntegrationTest, RequestTrailersOnly) {
  initialize();
  auto request = createRequest(empty_metadata_);
  const Http::TestResponseTrailerMapImpl reply_headers{{":status", "200"}, {"grpc-status", "0"}};
  EXPECT_CALL(*request->child_span_, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("0")));
  EXPECT_CALL(*request->child_span_,
              setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*request, onFailure(Status::Internal, "", _)).WillExitIfNeeded();
  dispatcher_helper_.setStreamEventPending();
  EXPECT_CALL(*request->child_span_, finishSpan());
  request->fake_stream_->encodeTrailers(reply_headers);
  dispatcher_helper_.runDispatcher();
}

// Validate that a trailers RESOURCE_EXHAUSTED reply is handled.
TEST_P(GrpcClientIntegrationTest, ResourceExhaustedError) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  dispatcher_helper_.runDispatcher();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::ResourceExhausted, "error message",
                             empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that a trailers Unauthenticated reply is handled.
TEST_P(GrpcClientIntegrationTest, UnauthenticatedError) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Unauthenticated, "error message",
                             empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that a trailers reply is still handled even if a grpc status code larger than
// MaximumKnown, is handled.
TEST_P(GrpcClientIntegrationTest, MaximumKnownPlusOne) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendServerTrailers(
      static_cast<Status::GrpcStatus>(Status::WellKnownGrpcStatus::MaximumKnown + 1),
      "error message", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that we can continue to receive after a local close.
TEST_P(GrpcClientIntegrationTest, ReceiveAfterLocalClose) {
  initialize();
  auto stream = createStream(empty_metadata_);

  RequestArgs request_args;
  request_args.end_stream = true;
  stream->sendRequest(request_args);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendReply();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that reset() doesn't explode on a half-closed stream (local).
TEST_P(GrpcClientIntegrationTest, ResetAfterCloseLocal) {
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->grpc_stream_->closeStream();
  ASSERT_TRUE(stream->fake_stream_->waitForEndStream(dispatcher_helper_.dispatcher_));
  stream->grpc_stream_->resetStream();
  dispatcher_helper_.dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(stream->fake_stream_->waitForReset());
}

// Validate that request cancel() works.
TEST_P(GrpcClientIntegrationTest, CancelRequest) {
  initialize();
  auto request = createRequest(empty_metadata_);
  EXPECT_CALL(*request->child_span_,
              setTag(Eq(Tracing::Tags::get().Status), Eq(Tracing::Tags::get().Canceled)));
  EXPECT_CALL(*request->child_span_, finishSpan());
  request->grpc_request_->cancel();
  dispatcher_helper_.dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(request->fake_stream_->waitForReset());
}

// Parameterize the loopback test server socket address and gRPC client type.
INSTANTIATE_TEST_SUITE_P(SslIpVersionsClientType, GrpcSslClientIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Validate that a simple request-reply unary RPC works with SSL.
TEST_P(GrpcSslClientIntegrationTest, BasicSslRequest) {
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that a simple request-reply unary RPC works with SSL + client certs.
TEST_P(GrpcSslClientIntegrationTest, BasicSslRequestWithClientCert) {
  use_client_cert_ = true;
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate TLS version mismatch between the client and the server.
TEST_P(GrpcSslClientIntegrationTest, BasicSslRequestHandshakeFailure) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.google_grpc_disable_tls_13", "true"}});
  use_server_tls_13_ = true;
  initialize();
  auto request = createRequest(empty_metadata_, false);
  EXPECT_CALL(*request->child_span_, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("13")));
  EXPECT_CALL(*request->child_span_,
              setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*request, onFailure(Status::Internal, "", _)).WillOnce(InvokeWithoutArgs([this]() {
    dispatcher_helper_.dispatcher_.exit();
  }));
  EXPECT_CALL(*request->child_span_, finishSpan());
  FakeRawConnectionPtr fake_connection;
  ASSERT_TRUE(fake_upstream_->waitForRawConnection(fake_connection));
  if (fake_connection->connected()) {
    ASSERT_TRUE(fake_connection->waitForDisconnect());
  }
  dispatcher_helper_.dispatcher_.run(Event::Dispatcher::RunType::Block);
}

#ifdef ENVOY_GOOGLE_GRPC
// AccessToken credential validation tests.
class GrpcAccessTokenClientIntegrationTest : public GrpcSslClientIntegrationTest {
public:
  void expectExtraHeaders(FakeStream& fake_stream) override {
    AssertionResult result = fake_stream.waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    const auto auth_headers = fake_stream.headers().get(Http::LowerCaseString("authorization"));
    if (!access_token_value_.empty()) {
      EXPECT_EQ("Bearer " + access_token_value_, auth_headers[0]->value().getStringView());
    }
    if (!access_token_value_2_.empty()) {
      EXPECT_EQ("Bearer " + access_token_value_2_, auth_headers[1]->value().getStringView());
    }
  }

  envoy::config::core::v3::GrpcService createGoogleGrpcConfig() override {
    auto config = GrpcClientIntegrationTest::createGoogleGrpcConfig();
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->set_credentials_factory_name(credentials_factory_name_);
    auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
    ssl_creds->mutable_root_certs()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    google_grpc->add_call_credentials()->set_access_token(access_token_value_);
    if (!access_token_value_2_.empty()) {
      google_grpc->add_call_credentials()->set_access_token(access_token_value_2_);
    }
    if (!refresh_token_value_.empty()) {
      google_grpc->add_call_credentials()->set_google_refresh_token(refresh_token_value_);
    }
    return config;
  }

  std::string access_token_value_{};
  std::string access_token_value_2_{};
  std::string refresh_token_value_{};
  std::string credentials_factory_name_{};
};

// Parameterize the loopback test server socket address and gRPC client type.
INSTANTIATE_TEST_SUITE_P(SslIpVersionsClientType, GrpcAccessTokenClientIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         GrpcClientIntegrationParamTest::protocolTestParamsToString);

// Validate that a simple request-reply unary RPC works with AccessToken auth.
TEST_P(GrpcAccessTokenClientIntegrationTest, AccessTokenAuthRequest) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  access_token_value_ = "accesstokenvalue";
  credentials_factory_name_ = "envoy.grpc_credentials.access_token_example";
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that a simple request-reply stream RPC works with AccessToken auth..
TEST_P(GrpcAccessTokenClientIntegrationTest, AccessTokenAuthStream) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  access_token_value_ = "accesstokenvalue";
  credentials_factory_name_ = "envoy.grpc_credentials.access_token_example";
  initialize();
  auto stream = createStream(empty_metadata_);
  stream->sendServerInitialMetadata(empty_metadata_);
  stream->sendRequest();
  stream->sendReply();
  stream->sendServerTrailers(Status::WellKnownGrpcStatus::Ok, "", empty_metadata_);
  dispatcher_helper_.runDispatcher();
}

// Validate that multiple access tokens are accepted
TEST_P(GrpcAccessTokenClientIntegrationTest, MultipleAccessTokens) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  access_token_value_ = "accesstokenvalue";
  access_token_value_2_ = "accesstokenvalue2";
  credentials_factory_name_ = "envoy.grpc_credentials.access_token_example";
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that extra params are accepted
TEST_P(GrpcAccessTokenClientIntegrationTest, ExtraCredentialParams) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  access_token_value_ = "accesstokenvalue";
  refresh_token_value_ = "refreshtokenvalue";
  credentials_factory_name_ = "envoy.grpc_credentials.access_token_example";
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that no access token still works
TEST_P(GrpcAccessTokenClientIntegrationTest, NoAccessTokens) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  credentials_factory_name_ = "envoy.grpc_credentials.access_token_example";
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that an unknown credentials factory name throws an EnvoyException
TEST_P(GrpcAccessTokenClientIntegrationTest, InvalidCredentialFactory) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  credentials_factory_name_ = "unknown";
  EXPECT_THROW_WITH_MESSAGE(initialize(), EnvoyException,
                            "Unknown google grpc credentials factory: unknown");
}

#endif

} // namespace
} // namespace Grpc
} // namespace Envoy
