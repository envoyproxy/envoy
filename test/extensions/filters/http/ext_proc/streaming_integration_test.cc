#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

#include "source/common/network/address_impl.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/test_processor.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::LowerCaseString;

// The buffer size for the listeners
static const uint32_t BufferSize = 100000;

// These tests exercise ext_proc using the integration test framework and a real gRPC server
// for the external processor. This lets us more fully exercise all the things that happen
// with larger, streamed payloads.
class StreamingIntegrationTest : public HttpIntegrationTest,
                                 public Grpc::GrpcClientIntegrationParamTest {

protected:
  StreamingIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    test_processor_.shutdown();
  }

  void initializeConfig() {
    // This enables a built-in automatic upstream server.
    autonomous_upstream_ = true;

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
      address->set_address("127.0.0.1");
      address->set_port_value(test_processor_.port());

      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC.
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));
      ConfigHelper::setHttp2(*processor_cluster);

      // Make sure both flavors of gRPC client use the right address.
      const auto addr =
          std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", test_processor_.port());
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server", addr);

      // Merge the filter.
      envoy::config::listener::v3::Filter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.addFilter(MessageUtil::getJsonStringFromMessageOrDie(ext_proc_filter));
    });

    // Make sure that we have control over when buffers will fill up.
    config_helper_.setBufferLimits(BufferSize, BufferSize);

    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  Http::RequestEncoder&
  sendClientRequestHeaders(absl::optional<std::function<void(Http::HeaderMap&)>> cb) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers, std::string("POST"));
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

  TestProcessor test_processor_;
  envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor proto_config_{};
  IntegrationStreamDecoderPtr client_response_;
};

// Ensure that the test suite is run with all combinations the Envoy and Google gRPC clients.
INSTANTIATE_TEST_SUITE_P(StreamingProtocols, StreamingIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Send a body that's larger than the buffer limit, and have the processor return immediately
// after the headers come in.
TEST_P(StreamingIntegrationTest, PostAndProcessHeadersOnly) {
  uint32_t num_chunks = 150;
  uint32_t chunk_size = 1000;

  // This starts the gRPC server in the background. It'll be shut down when we stop the tests.
  test_processor_.start(
      [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        if (!stream->Read(&header_req)) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
        }
        if (!header_req.has_request_headers()) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected request headers");
        }

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        stream->Write(header_resp);
        return grpc::Status::OK;
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto& encoder = sendClientRequestHeaders([num_chunks, chunk_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), num_chunks * chunk_size);
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
      [total_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        if (!stream->Read(&header_req)) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
        }
        if (!header_req.has_request_headers()) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected request headers");
        }

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        auto* override = header_resp.mutable_mode_override();
        override->set_request_body_mode(ProcessingMode::BUFFERED);
        stream->Write(header_resp);

        ProcessingRequest body_req;
        if (!stream->Read(&body_req)) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
        }
        if (!body_req.has_request_body()) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected request body");
        }
        if (body_req.request_body().body().size() != total_size) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "incorrect body size");
        }

        ProcessingResponse body_resp;
        header_resp.mutable_request_body();
        stream->Write(body_resp);

        return grpc::Status::OK;
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto& encoder = sendClientRequestHeaders([total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
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

// Do an HTTP GET that will return a body smaller than the buffer limit, which we process
// in the processor.
TEST_P(StreamingIntegrationTest, GetAndProcessBufferedResponseBody) {
  uint32_t response_size = 90000;

  test_processor_.start(
      [response_size](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        if (!stream->Read(&header_req)) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
        }
        if (!header_req.has_request_headers()) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected request headers");
        }

        ProcessingResponse header_resp;
        header_resp.mutable_request_headers();
        auto* override = header_resp.mutable_mode_override();
        override->set_response_header_mode(ProcessingMode::SKIP);
        override->set_response_body_mode(ProcessingMode::BUFFERED);
        stream->Write(header_resp);

        ProcessingRequest body_req;
        if (!stream->Read(&body_req)) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
        }
        if (!body_req.has_response_body()) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected response body");
        }
        if (body_req.response_body().body().size() != response_size) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "incorrect body size");
        }

        return grpc::Status::OK;
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

// Send a body that's larger than the buffer limit and have the processor
// try to process it in buffered mode. The client should get an error.
TEST_P(StreamingIntegrationTest, PostAndProcessBufferedRequestBodyTooBig) {
  // Send just one chunk beyond the buffer limit -- integration
  // test framework can't handle anything else.
  const uint32_t num_chunks = 11;
  const uint32_t chunk_size = 10000;
  uint32_t total_size = num_chunks * chunk_size;

  test_processor_.start(
      [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        if (!stream->Read(&header_req)) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
        }
        if (!header_req.has_request_headers()) {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected request headers");
        }

        ProcessingResponse response;
        response.mutable_request_headers();
        auto* override = response.mutable_mode_override();
        override->set_request_body_mode(ProcessingMode::BUFFERED);
        stream->Write(response);

        ProcessingRequest header_resp;
        if (stream->Read(&header_resp)) {
          if (!header_resp.has_response_headers()) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected response headers");
          }
        }
        return grpc::Status::OK;
      });

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto& encoder = sendClientRequestHeaders([total_size](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("expect_request_size_bytes"), total_size);
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
  EXPECT_THAT(client_response_->headers(), Http::HttpStatusIs("413"));
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
