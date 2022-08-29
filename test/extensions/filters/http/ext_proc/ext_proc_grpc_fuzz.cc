// TODO(ikepolinsky): Major action items to improve this fuzzer
// 1. Move external process from separate thread to have test all in one thread
//    - Explore using fake gRPC client for this
// 2. Implement sending trailers from downstream and mutating headers/trailers
//    in the external process.
// 3. Use an upstream that sends varying responses (also with trailers)
// 4. Explore performance optimizations:
//    - Threads and fake gRPC client above might help
//    - Local testing had almost 800k inline 8 bit counters resulting in ~3
//      exec/s. How far can we reduce the number of counters?
//    - At the loss of reproducibility use a persistent envoy
// 5. Protobuf fuzzing would greatly increase crash test case readability
//    - How will this impact speed?
//    - Can it be done on single thread as well?
// 6. Restructure to inherit common functions between ExtProcIntegrationTest
//    and this class. This involves adding a new ExtProcIntegrationBase class
//    common to both.
// 7. Remove locks after crash is addressed by separate issue

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "source/common/network/address_impl.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz_helper.h"
#include "test/extensions/filters/http/ext_proc/test_processor.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

// The buffer size for the listeners
static const uint32_t BufferSize = 100000;

// These tests exercise the ext_proc filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.

class ExtProcIntegrationFuzz : public HttpIntegrationTest,
                               public Grpc::BaseGrpcClientIntegrationParamTest {
public:
  ExtProcIntegrationFuzz(Network::Address::IpVersion ip_version, Grpc::ClientType client_type)
      : HttpIntegrationTest(Http::CodecType::HTTP2, ip_version) {
    ip_version_ = ip_version;
    client_type_ = client_type;
  }

  void tearDown() {
    cleanupUpstreamAndDownstream();
    test_processor_.shutdown();
  }

  Network::Address::IpVersion ipVersion() const override { return ip_version_; }
  Grpc::ClientType clientType() const override { return client_type_; }

  void initializeFuzzer(bool autonomous_upstream) {
    autonomous_upstream_ = autonomous_upstream;
    autonomous_allow_incomplete_streams_ = true;
    initializeConfig();
    HttpIntegrationTest::initialize();
  }

  void initializeConfig() {
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

      // Merge the filter.
      envoy::config::listener::v3::Filter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrDie(ext_proc_filter));
    });

    // Make sure that we have control over when buffers will fill up.
    config_helper_.setBufferLimits(BufferSize, BufferSize);

    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      absl::string_view http_method = "GET") {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{{":method", std::string(http_method)}};
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    HttpTestUtility::addDefaultHeaders(headers, false);
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithBody(
      absl::string_view body,
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      absl::string_view http_method = "POST") {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{{":method", std::string(http_method)}};
    HttpTestUtility::addDefaultHeaders(headers, false);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeRequestWithBody(headers, std::string(body));
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithChunks(
      FuzzedDataProvider* fdp, ExtProcFuzzHelper* fh,
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      absl::string_view http_method = "POST") {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{{":method", std::string(http_method)}};
    HttpTestUtility::addDefaultHeaders(headers, false);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    auto encoder_decoder = codec_client_->startRequest(headers);
    IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
    auto& encoder = encoder_decoder.first;

    const uint32_t num_chunks =
        fdp->ConsumeIntegralInRange<uint32_t>(0, ExtProcFuzzMaxStreamChunks);
    for (uint32_t i = 0; i < num_chunks; i++) {
      // TODO(ikepolinsky): open issue for this crash and remove locks once
      // fixed.
      // If proxy closes connection before body is fully sent it causes a
      // crash. To address this, the external processor sets a flag to
      // signal when it has generated an immediate response which will close
      // the connection in the future. We check this flag, which is protected
      // by a lock, before sending a chunk. If the flag is set, we don't attempt
      // to send more data, regardless of whether or not the
      // codec_client connection is still open. There are no locks protecting
      // the codec_client connection and cannot trust that it's safe to send
      // another chunk
      fh->immediate_resp_lock_.lock();
      if (fh->immediate_resp_sent_) {
        ENVOY_LOG_MISC(trace, "Proxy closed connection, returning early");
        fh->immediate_resp_lock_.unlock();
        return response;
      }
      const uint32_t data_size = fdp->ConsumeIntegralInRange<uint32_t>(0, ExtProcFuzzMaxDataSize);
      ENVOY_LOG_MISC(trace, "Sending chunk of {} bytes", data_size);
      codec_client_->sendData(encoder, data_size, false);
      fh->immediate_resp_lock_.unlock();
    }

    // See comment above
    fh->immediate_resp_lock_.lock();
    if (!fh->immediate_resp_sent_) {
      ENVOY_LOG_MISC(trace, "Sending empty chunk to close stream");
      Buffer::OwnedImpl empty_chunk;
      codec_client_->sendData(encoder, empty_chunk, true);
    }
    fh->immediate_resp_lock_.unlock();
    return response;
  }

  IntegrationStreamDecoderPtr randomDownstreamRequest(FuzzedDataProvider* fdp,
                                                      ExtProcFuzzHelper* fh) {
    // From the external processor's view each of these requests
    // are handled the same way. They only differ in what the server should
    // send back to the client.
    // TODO(ikepolinsky): add random flag for sending trailers with a request
    //   using HttpIntegration::sendTrailers()
    switch (fdp->ConsumeEnum<HttpMethod>()) {
    case HttpMethod::GET:
      ENVOY_LOG_MISC(trace, "Sending GET request");
      return sendDownstreamRequest(absl::nullopt);
    case HttpMethod::POST:
      if (fdp->ConsumeBool()) {
        ENVOY_LOG_MISC(trace, "Sending POST request with body");
        const uint32_t data_size = fdp->ConsumeIntegralInRange<uint32_t>(0, ExtProcFuzzMaxDataSize);
        const std::string data = std::string(data_size, 'a');
        return sendDownstreamRequestWithBody(data, absl::nullopt);
      } else {
        ENVOY_LOG_MISC(trace, "Sending POST request with chunked body");
        return sendDownstreamRequestWithChunks(fdp, fh, absl::nullopt);
      }
    default:
      RELEASE_ASSERT(false, "Unhandled HttpMethod");
    }
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  TestProcessor test_processor_;
  Network::Address::IpVersion ip_version_;
  Grpc::ClientType client_type_;
};

DEFINE_PROTO_FUZZER(const test::extensions::filters::http::ext_proc::ExtProcGrpcTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  // have separate data providers.
  FuzzedDataProvider downstream_provider(
      reinterpret_cast<const uint8_t*>(input.downstream_data().data()),
      input.downstream_data().size());
  FuzzedDataProvider ext_proc_provider(
      reinterpret_cast<const uint8_t*>(input.ext_proc_data().data()), input.ext_proc_data().size());

  // Get IP and gRPC version from environment
  ExtProcIntegrationFuzz fuzzer(TestEnvironment::getIpVersionsForTest()[0],
                                TestEnvironment::getsGrpcVersionsForTest()[0]);
  ExtProcFuzzHelper fuzz_helper(&ext_proc_provider);

  // This starts an external processor in a separate thread. This allows for the
  // external process to consume messages in a loop without blocking the fuzz
  // target from receiving the response.
  fuzzer.test_processor_.start(
      fuzzer.ip_version_,
      [&fuzz_helper](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        while (true) {
          ProcessingRequest req;
          if (!stream->Read(&req)) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
          }

          fuzz_helper.logRequest(&req);

          // The following blocks generate random data for the 9 fields of the
          // ProcessingResponse gRPC message

          // 1 - 7. Randomize response
          // If true, immediately close the connection with a random Grpc Status.
          // Otherwise randomize the response
          ProcessingResponse resp;
          if (fuzz_helper.provider_->ConsumeBool()) {
            ENVOY_LOG_MISC(trace, "Immediately Closing gRPC connection");
            return fuzz_helper.randomGrpcStatusWithMessage();
          } else {
            ENVOY_LOG_MISC(trace, "Generating Random ProcessingResponse");
            fuzz_helper.randomizeResponse(&resp, &req);
          }

          // 8. Randomize dynamic_metadata
          // TODO(ikepolinsky): ext_proc does not support dynamic_metadata

          // 9. Randomize mode_override
          if (fuzz_helper.provider_->ConsumeBool()) {
            ENVOY_LOG_MISC(trace, "Generating Random ProcessingMode Override");
            ProcessingMode* msg = resp.mutable_mode_override();
            fuzz_helper.randomizeOverrideResponse(msg);
          }

          ENVOY_LOG_MISC(trace, "Response generated, writing to stream.");
          stream->Write(resp);
        }

        return grpc::Status::OK;
      });

  ENVOY_LOG_MISC(trace, "External Process started.");

  fuzzer.initializeFuzzer(true);
  ENVOY_LOG_MISC(trace, "Fuzzer initialized");

  const auto response = fuzzer.randomDownstreamRequest(&downstream_provider, &fuzz_helper);

  // For fuzz testing we don't care about the response code, only that
  // the stream ended in some graceful manner
  ENVOY_LOG_MISC(trace, "Waiting for response.");
  if (response->waitForEndStream(std::chrono::milliseconds(200))) {
    ENVOY_LOG_MISC(trace, "Response received.");
  } else {
    // TODO(ikepolinsky): investigate if there is anyway around this.
    // Waiting too long for a fuzz case to fail will drastically
    // reduce executions/second.
    ENVOY_LOG_MISC(trace, "Response timed out.");
  }
  fuzzer.tearDown();
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
