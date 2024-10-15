#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/common/hash.h"
#include "source/common/network/address_impl.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/test_processor.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

using Http::LowerCaseString;

// The buffer size for the listeners
static const uint32_t BufferSize = 100000;

// These tests exercise ext_proc using the integration test framework and a real gRPC server
// for the external processor. This lets us more fully exercise all the things that happen
// with larger, streamed payloads.
class StreamingIntegrationTest : public HttpIntegrationTest,
                                 public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {

protected:
  StreamingIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}
  // TODO(yanjunxiang-google): Verify that bypassing virtual dispatch here was intentional
  // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
  ~StreamingIntegrationTest() override { StreamingIntegrationTest::TearDown(); }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    test_processor_.shutdown();
  }

  void initializeConfig() {
    skip_tag_extraction_rule_check_ = true;
    // This enables a built-in automatic upstream server.
    autonomous_upstream_ = true;
    autonomous_allow_incomplete_streams_ = true;
    proto_config_.set_allow_mode_override(true);
    proto_config_.mutable_message_timeout()->set_seconds(2);
    proto_config_.set_failure_mode_allow(true);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Create a cluster for our gRPC server pointing to the address that is running the gRPC
      // server.
      auto* processor_cluster = bootstrap.mutable_static_resources()->add_clusters();
      processor_cluster->set_name("ext_proc_server");
      processor_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");
      auto* address = processor_cluster->mutable_load_assignment()
                          ->add_endpoints()
                          ->add_lb_endpoints()
                          ->mutable_endpoint()
                          ->mutable_address()
                          ->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      address->set_port_value(test_processor_.port());

      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC.
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));
      ConfigHelper::setHttp2(*processor_cluster);

      // Make sure both flavors of gRPC client use the right address.
      const auto addr = Network::Test::getCanonicalLoopbackAddress(ipVersion());
      const auto addr_port = Network::Utility::getAddressWithPort(*addr, test_processor_.port());
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server", addr_port);
      // Insert some extra metadata. This ensures that we are actually passing the
      // "stream info" from the original HTTP request all the way down to the
      // ext_proc stream.
      auto* metadata = proto_config_.mutable_grpc_service()->mutable_initial_metadata()->Add();
      metadata->set_key("x-request-id");
      metadata->set_value("%REQ(x-request-id)%");

      // Merge the filter.
      envoy::config::listener::v3::Filter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
    });

    // Make sure that we have control over when buffers will fill up.
    config_helper_.setBufferLimits(BufferSize, BufferSize);
    // Parameterize with defer processing to prevent bit rot as filter made
    // assumptions of data flow, prior relying on eager processing.
    config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                      deferredProcessing() ? "true" : "false");

    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  Http::RequestEncoder&
  sendClientRequestHeaders(absl::optional<std::function<void(Http::HeaderMap&)>> cb) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{{":method", "POST"}};
    HttpTestUtility::addDefaultHeaders(headers, false);
    if (cb) {
      (*cb)(headers);
    }
    auto encoder_decoder = codec_client_->startRequest(headers);
    client_response_ = std::move(encoder_decoder.second);
    return encoder_decoder.first;
  }

  void sendGetRequest(const Http::RequestHeaderMap& headers) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    client_response_ = codec_client_->makeHeaderOnlyRequest(headers);
  }

  // Send an HTTP POST containing a randomly-generated body consisting of
  // "num_chunks" of "chunk_size" bytes each. Return a copy of the complete
  // body that may be used for comparison later.
  Buffer::OwnedImpl sendPostRequest(uint32_t num_chunks, uint32_t chunk_size,
                                    absl::optional<std::function<void(Http::HeaderMap&)>> cb) {
    auto& encoder = sendClientRequestHeaders(cb);
    Buffer::OwnedImpl post_body;
    for (uint32_t i = 0; i < num_chunks && codec_client_->streamOpen(); i++) {
      Buffer::OwnedImpl chunk;
      TestUtility::feedBufferWithRandomCharacters(chunk, chunk_size);
      post_body.add(chunk.toString());
      codec_client_->sendData(encoder, chunk, false);
    }
    if (codec_client_->streamOpen()) {
      Buffer::OwnedImpl empty_chunk;
      codec_client_->sendData(encoder, empty_chunk, true);
    }
    return post_body;
  }

  TestProcessor test_processor_;
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  IntegrationStreamDecoderPtr client_response_;
  std::atomic<uint64_t> processor_request_hash_;
  std::atomic<uint64_t> processor_response_hash_;
};

// Ensure that the test suite is run with all combinations the Envoy and Google gRPC clients.
INSTANTIATE_TEST_SUITE_P(
    StreamingProtocols, StreamingIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

// Send a body that's larger than the buffer limit, and have the processor return immediately
// after the headers come in. Also check the metadata in this test.
TEST_P(StreamingIntegrationTest, PostAndProcessHeadersOnly) {
  uint32_t num_chunks = 150;
  uint32_t chunk_size = 1000;

  // This starts the gRPC server in the background. It'll be shut down when we stop the tests.
  test_processor_.start(
      ipVersion(),
      [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        // This is the same gRPC stream processing code that a "user" of ext_proc
        // would write. In this case, we expect to receive a request_headers
        // message, and then close the stream.
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);
        // Returning here closes the stream, unless we had an ASSERT failure
        // previously.
      },
      [](grpc::ServerContext* ctx) {
        // Verify that the metadata set in the grpc client configuration
        // above is actually sent to our RPC.
        auto request_id = ctx->client_metadata().find("x-request-id");
        ASSERT_NE(request_id, ctx->client_metadata().end());
        EXPECT_EQ(request_id->second, "sent some metadata");
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto& encoder = sendClientRequestHeaders([num_chunks, chunk_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), num_chunks * chunk_size);
    headers.addCopy(LowerCaseString("x-request-id"), "sent some metadata");
  });

  for (uint32_t i = 0; i < num_chunks; i++) {
    Buffer::OwnedImpl chunk;
    TestUtility::feedBufferWithRandomCharacters(chunk, chunk_size);
    codec_client_->sendData(encoder, chunk, false);
  }
  Buffer::OwnedImpl empty_chunk;
  codec_client_->sendData(encoder, empty_chunk, true);

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

// Send a body that's smaller than the buffer limit, and have the processor
// request to see it in buffered form before allowing it to continue.
TEST_P(StreamingIntegrationTest, PostAndProcessBufferedRequestBody) {
  const uint32_t num_chunks = 99;
  const uint32_t chunk_size = 1000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(),
      [total_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        auto* override = header_resp.mutable_mode_override();
        override->set_request_body_mode(ProcessingMode::BUFFERED);
        stream->Write(header_resp);

        ProcessingRequest body_req;
        ASSERT_TRUE(stream->Read(&body_req));
        ASSERT_TRUE(body_req.has_request_body());
        EXPECT_EQ(body_req.request_body().body().size(), total_size);

        ProcessingResponse body_resp;
        body_resp.mutable_request_body();
        stream->Write(body_resp);
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

// Send a body that's larger than the buffer limit in streamed mode, and ensure
// that the processor gets the right number of bytes.
TEST_P(StreamingIntegrationTest, PostAndProcessStreamedRequestBody) {
  const uint32_t num_chunks = 152;
  const uint32_t chunk_size = 1000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(),
      [total_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        // Expect a request_headers message as the first message on the stream,
        // and send back an empty response.
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());
        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);

        // Now, expect a bunch of request_body messages and respond to each.
        // Count up the number of bytes we receive and make sure that we get
        // them all.
        uint32_t received_size = 0;
        ProcessingRequest body_req;
        do {
          ASSERT_TRUE(stream->Read(&body_req));
          ASSERT_TRUE(body_req.has_request_body());
          received_size += body_req.request_body().body().size();
          ProcessingResponse body_resp;
          body_resp.mutable_request_body();
          stream->Write(body_resp);
        } while (!body_req.request_body().end_of_stream());

        EXPECT_EQ(received_size, total_size);
      });

  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    // This header tells the "autonomous upstream" that will respond to our
    // request to throw an error if it doesn't get the right number of bytes.
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

// Send a body that's larger than the buffer limit in streamed mode, and change
// the processing mode after receiving some of the body. We might continue to
// receive streamed messages after this point, but the whole message should
// make it upstream regardless.
TEST_P(StreamingIntegrationTest, PostAndProcessStreamedRequestBodyPartially) {
  const uint32_t num_chunks = 19;
  const uint32_t chunk_size = 10000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(), [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());
        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);

        uint32_t received_count = 0;
        ProcessingRequest req;

        // Expect body chunks, and also change the processing mode partway so
        // that we can see what happens when we do that.
        while (stream->Read(&req)) {
          ProcessingResponse resp;
          if (req.has_request_body()) {
            received_count++;
            if (received_count == 1) {
              // After first body chunk, change the processing mode. Since the body
              // is pipelined, we might still get body chunks, however. This test can't
              // validate this, but at least we can ensure that this doesn't blow up the
              // protocol.
              auto* mode_override = resp.mutable_mode_override();
              mode_override->set_request_body_mode(ProcessingMode::NONE);
            }
            resp.mutable_request_body();
          } else if (req.has_response_headers()) {
            // Should not see response headers until we changed the processing mode.
            EXPECT_GE(received_count, 1);
            resp.mutable_response_headers();
          } else {
            FAIL() << "unexpected stream message";
          }
          stream->Write(resp);
        }
      });

  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

// Send a body that's larger than the buffer limit in streamed mode, and close
// the stream before we've received all the chunks. The whole message should
// be received by the upstream regardless.
TEST_P(StreamingIntegrationTest, PostAndProcessStreamedRequestBodyAndClose) {
  const uint32_t num_chunks = 25;
  const uint32_t chunk_size = 10000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(),
      [total_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());
        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);

        ProcessingRequest req;
        uint32_t received_size = 0;
        while (stream->Read(&req)) {
          received_size += req.request_body().body().size();
          if (received_size > (total_size / 2)) {
            // Return before we get the end of stream
            return;
          }
          ProcessingResponse resp;
          resp.mutable_request_body();
          stream->Write(resp);
        }
      });

  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

TEST_P(StreamingIntegrationTest, PostAndProcessStreamedRequestBodyObservabilityMode) {
  const uint32_t num_chunks = 152;
  const uint32_t chunk_size = 1000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(),
      [total_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        // Expect a request_headers message as the first message on the stream,
        // and send back an empty response.
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());
        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);

        // Now, expect a bunch of request_body messages and respond to each.
        // Count up the number of bytes we receive and make sure that we get
        // them all.
        uint32_t received_size = 0;
        ProcessingRequest body_req;
        do {
          ASSERT_TRUE(stream->Read(&body_req));
          ASSERT_TRUE(body_req.has_request_body());
          received_size += body_req.request_body().body().size();
          ProcessingResponse body_resp;
          body_resp.mutable_request_body();
          stream->Write(body_resp);
        } while (!body_req.request_body().end_of_stream());

        EXPECT_EQ(received_size, total_size);
      });

  // Enable observability mode.
  proto_config_.set_observability_mode(true);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    // This header tells the "autonomous upstream" that will respond to our
    // request to throw an error if it doesn't get the right number of bytes.
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}
// Do an HTTP GET that will return a body smaller than the buffer limit, which we process
// in the processor.
TEST_P(StreamingIntegrationTest, DISABLED_GetAndProcessBufferedResponseBody) {
  uint32_t response_size = 90000;

  test_processor_.start(
      ipVersion(),
      [response_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        auto* override = header_resp.mutable_mode_override();
        override->set_response_header_mode(ProcessingMode::SKIP);
        override->set_response_body_mode(ProcessingMode::BUFFERED);
        stream->Write(header_resp);

        ProcessingRequest body_req;
        ASSERT_TRUE(stream->Read(&body_req));
        ASSERT_TRUE(body_req.has_response_body());
        EXPECT_EQ(body_req.response_body().body().size(), response_size);
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy(LowerCaseString("response_size_bytes"), response_size);
  sendGetRequest(headers);

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
  EXPECT_EQ(client_response_->body().size(), response_size);
}

// Do an HTTP GET that will return a body larger than the buffer limit, which we process
// in the processor using streaming.
TEST_P(StreamingIntegrationTest, DISABLED_GetAndProcessStreamedResponseBody) {
  uint32_t response_size = 170000;

  test_processor_.start(
      ipVersion(), [this, response_size](
                       grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        auto* override = header_resp.mutable_mode_override();
        override->set_response_header_mode(ProcessingMode::SKIP);
        override->set_response_body_mode(ProcessingMode::STREAMED);
        stream->Write(header_resp);

        ProcessingRequest body_req;
        uint32_t total_response_size = 0;
        Buffer::OwnedImpl allData;

        do {
          ASSERT_TRUE(stream->Read(&body_req));
          ASSERT_TRUE(body_req.has_response_body());
          total_response_size += body_req.response_body().body().size();
          // Save all the chunks in a buffer so that we can calculate a hash.
          allData.add(body_req.response_body().body());
          ProcessingResponse body_resp;
          body_resp.mutable_response_body();
          stream->Write(body_resp);
        } while (!body_req.response_body().end_of_stream());
        processor_response_hash_ = HashUtil::xxHash64(allData.toString());
        ASSERT_EQ(total_response_size, response_size);
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  // This magic header tells the "autonomous upstream" to send back a response
  // of the specified size.
  headers.addCopy(LowerCaseString("response_size_bytes"), response_size);
  sendGetRequest(headers);

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
  EXPECT_EQ(client_response_->body().size(), response_size);
  test_processor_.shutdown();
  EXPECT_EQ(processor_response_hash_, HashUtil::xxHash64(client_response_->body()));
}

// Send a large HTTP POST, and expect back an equally large reply. Stream both and verify
// that we got back what we expected. The processor itself must be written carefully
// because once the request headers are delivered, the request and response body
// chunks and the response headers can come in any order.
TEST_P(StreamingIntegrationTest, DISABLED_PostAndProcessStreamBothBodies) {
  const uint32_t send_chunks = 10;
  const uint32_t chunk_size = 11000;
  uint32_t request_size = send_chunks * chunk_size;
  uint32_t response_size = 1700000;

  test_processor_.start(
      ipVersion(), [this, request_size, response_size](
                       grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());
        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);

        bool saw_response_headers = false;
        bool saw_request_eof = false;
        bool saw_response_eof = false;
        ProcessingRequest message;
        uint32_t total_request_size = 0;
        uint32_t total_response_size = 0;
        Buffer::OwnedImpl allResponseData;
        Buffer::OwnedImpl allRequestData;

        do {
          ProcessingResponse response;
          ASSERT_TRUE(stream->Read(&message));
          if (message.has_response_headers()) {
            // Expect only one response headers message, with a good status.
            EXPECT_FALSE(saw_response_headers);
            EXPECT_THAT(message.response_headers().headers(),
                        SingleProtoHeaderValueIs(":status", "200"));
            saw_response_headers = true;
            response.mutable_response_headers();
          } else if (message.has_request_body()) {
            // Expect a number of request body messages. Make sure that we
            // don't get a duplicate EOF, count the size, and store the chunks
            // so that we can calculate a hash.
            total_request_size += message.request_body().body().size();
            allRequestData.add(message.request_body().body());
            if (message.request_body().end_of_stream()) {
              EXPECT_FALSE(saw_request_eof);
              saw_request_eof = true;
              EXPECT_EQ(total_request_size, request_size);
              processor_request_hash_ = HashUtil::xxHash64(allRequestData.toString());
            }
            response.mutable_request_body();
          } else if (message.has_response_body()) {
            // Count ans hash the response body like we did for the request body.
            total_response_size += message.response_body().body().size();
            allResponseData.add(message.response_body().body());
            if (message.response_body().end_of_stream()) {
              EXPECT_FALSE(saw_response_eof);
              saw_response_eof = true;
              EXPECT_EQ(total_response_size, response_size);
              processor_response_hash_ = HashUtil::xxHash64(allResponseData.toString());
            }
            response.mutable_response_body();
          } else {
            FAIL() << "unexpected stream message";
          }
          stream->Write(response);
        } while (!(saw_response_headers && saw_request_eof && saw_response_eof));
      });

  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto request_body = sendPostRequest(
      send_chunks, chunk_size, [request_size, response_size](Http::HeaderMap& headers) {
        // Tell the upstream to fail if it doesn't get the right amount of data.
        headers.addCopy(LowerCaseString("expect_request_size_bytes"), request_size);
        // Also tell the upstream how much data to send back.
        headers.addCopy(LowerCaseString("response_size_bytes"), response_size);
      });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
  EXPECT_EQ(client_response_->body().size(), response_size);
  EXPECT_EQ(processor_request_hash_, HashUtil::xxHash64(request_body.toString()));
  EXPECT_EQ(processor_response_hash_, HashUtil::xxHash64(client_response_->body()));
}

// Send a large HTTP POST, and expect back an equally large reply. Stream both and replace both
// the request and response bodies with different bodies.
TEST_P(StreamingIntegrationTest, DISABLED_PostAndStreamAndTransformBothBodies) {
  const uint32_t send_chunks = 12;
  const uint32_t chunk_size = 10000;
  uint32_t response_size = 180000;

  test_processor_.start(
      ipVersion(), [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);

        bool saw_response_headers = false;
        bool saw_request_eof = false;
        bool saw_response_eof = false;
        bool first_request_chunk = true;
        ProcessingRequest message;

        do {
          ProcessingResponse response;
          ASSERT_TRUE(stream->Read(&message));
          if (message.has_response_headers()) {
            EXPECT_FALSE(saw_response_headers);
            EXPECT_THAT(message.response_headers().headers(),
                        SingleProtoHeaderValueIs(":status", "200"));
            saw_response_headers = true;
            response.mutable_response_headers();
          } else if (message.has_request_body()) {
            // Replace the first chunk with a new message, and zero out the rest
            auto* new_body = response.mutable_request_body()->mutable_response();
            if (first_request_chunk) {
              new_body->mutable_body_mutation()->set_body("Hello");
              first_request_chunk = false;
            } else {
              new_body->mutable_body_mutation()->set_clear_body(true);
            }
            if (message.request_body().end_of_stream()) {
              saw_request_eof = true;
            }
          } else if (message.has_response_body()) {
            // Replace the last chunk with a new message and zero out the rest
            auto* new_body = response.mutable_response_body()->mutable_response();
            if (message.response_body().end_of_stream()) {
              new_body->mutable_body_mutation()->set_body("World");
              first_request_chunk = false;
              saw_response_eof = true;
            } else {
              new_body->mutable_body_mutation()->set_clear_body(true);
            }
          } else {
            FAIL() << "unexpected stream message";
          }
          stream->Write(response);
        } while (!saw_response_headers || !saw_request_eof || !saw_response_eof);
      });

  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::STREAMED);
  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(send_chunks, chunk_size, [response_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), 5);
    headers.addCopy(LowerCaseString("response_size_bytes"), response_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

// Send a body that's larger than the buffer limit and have the processor
// try to process it in buffered mode. The client should get an error.
TEST_P(StreamingIntegrationTest, DISABLED_PostAndProcessBufferedRequestBodyTooBig) {
  // Send just one chunk beyond the buffer limit -- integration
  // test framework can't handle anything else.
  const uint32_t num_chunks = 11;
  const uint32_t chunk_size = 10000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(), [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse response;
        response.mutable_request_headers();
        auto* override = response.mutable_mode_override();
        override->set_request_body_mode(ProcessingMode::BUFFERED);
        stream->Write(response);

        ProcessingRequest header_resp;
        bool seen_response_headers = false;

        // Reading from the stream, we receive the response headers
        // later due to executing the local reply after the filter executes.
        const int num_reads_for_response_headers = 2;
        for (int i = 0; i < num_reads_for_response_headers; ++i) {
          if (stream->Read(&header_resp) && header_resp.has_response_headers()) {
            seen_response_headers = true;
            break;
          }
        }

        ASSERT_TRUE(seen_response_headers);
      });

  // Increase beyond the default to avoid timing out before getting the
  // sidecar response.
  proto_config_.mutable_message_timeout()->set_seconds(2);
  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("413"));
}

// Send a body that's smaller than the buffer limit, and have the processor
// request to see it in "buffered partial" form before allowing it to continue.
TEST_P(StreamingIntegrationTest, PostAndProcessBufferedPartialRequestBody) {
  const uint32_t num_chunks = 99;
  const uint32_t chunk_size = 1000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(),
      [total_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        auto* override = header_resp.mutable_mode_override();
        override->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
        stream->Write(header_resp);

        ProcessingRequest body_req;
        ASSERT_TRUE(stream->Read(&body_req));
        ASSERT_TRUE(body_req.has_request_body());
        EXPECT_TRUE(body_req.request_body().end_of_stream());
        EXPECT_EQ(body_req.request_body().body().size(), total_size);

        ProcessingResponse body_resp;
        body_resp.mutable_request_body();
        stream->Write(body_resp);
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

// Send a body that's larger than the buffer limit, and have the processor
// request to see it in "buffered partial" form before allowing it to continue.
// The processor should see only part of the message.
TEST_P(StreamingIntegrationTest, PostAndProcessBufferedPartialBigRequestBody) {
  const uint32_t num_chunks = 213;
  const uint32_t chunk_size = 1000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      ipVersion(),
      [total_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        auto* override = header_resp.mutable_mode_override();
        override->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
        stream->Write(header_resp);

        ProcessingRequest body_req;
        ASSERT_TRUE(stream->Read(&body_req));
        ASSERT_TRUE(body_req.has_request_body());
        EXPECT_FALSE(body_req.request_body().end_of_stream());
        EXPECT_LT(body_req.request_body().body().size(), total_size);

        ProcessingResponse body_resp;
        body_resp.mutable_request_body();
        stream->Write(body_resp);
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  sendPostRequest(num_chunks, chunk_size, [total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
  });

  ASSERT_TRUE(client_response_->waitForEndStream());
  EXPECT_TRUE(client_response_->complete());
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("200"));
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
