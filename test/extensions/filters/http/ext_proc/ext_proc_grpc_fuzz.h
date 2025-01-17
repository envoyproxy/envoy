#pragma once

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
// 5. Protobuf fuzzing would greatly increase crash test case readability
//    - How will this impact speed?
//    - Can it be done on single thread as well?
// 6. Restructure to inherit common functions between ExtProcIntegrationTest
//    and this class. This involves adding a new ExtProcIntegrationBase class
//    common to both.

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

namespace {
bool fuzzCreateEnvoy(const uint32_t exec, const bool persistent_mode) {
  const uint32_t exec_before_reset_envoy = 2000;
  return (!persistent_mode || exec % exec_before_reset_envoy == 0);
}
} // namespace

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

  void tearDown(bool keep_test_server) {
    cleanupUpstreamAndDownstream();
    codec_client_ = nullptr;
    if (!keep_test_server) {
      test_processor_.shutdown();
    }
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
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
    });

    // Make sure that we have control over when buffers will fill up.
    config_helper_.setBufferLimits(BufferSize, BufferSize);

    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      absl::string_view http_method = "GET") {
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
    Http::TestRequestHeaderMapImpl headers{{":method", std::string(http_method)}};
    HttpTestUtility::addDefaultHeaders(headers, false);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeRequestWithBody(headers, std::string(body));
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithChunks(
      FuzzedDataProvider* fdp,
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      absl::string_view http_method = "POST") {
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
      if (codec_client_->streamOpen()) {
        const uint32_t data_size = fdp->ConsumeIntegralInRange<uint32_t>(0, ExtProcFuzzMaxDataSize);
        ENVOY_LOG_MISC(trace, "Sending chunk of {} bytes", data_size);
        codec_client_->sendData(encoder, data_size, false);
      } else {
        ENVOY_LOG_MISC(trace, "Proxy closed connection, returning early");
        return response;
      }
    }
    if (codec_client_->streamOpen()) {
      ENVOY_LOG_MISC(trace, "Sending empty chunk to close stream");
      Buffer::OwnedImpl empty_chunk;
      codec_client_->sendData(encoder, empty_chunk, true);
    }
    return response;
  }

  IntegrationStreamDecoderPtr randomDownstreamRequest(FuzzedDataProvider* fdp) {
    if (!codec_client_ || codec_client_->disconnected()) {
      auto conn = makeClientConnection(lookupPort("http"));
      codec_client_ = makeHttpConnection(std::move(conn));
    }
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
        return sendDownstreamRequestWithChunks(fdp, absl::nullopt);
      }
      break;
    default:
      RELEASE_ASSERT(false, "Unhandled HttpMethod");
    }
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
  TestProcessor test_processor_;
  Network::Address::IpVersion ip_version_;
  Grpc::ClientType client_type_;
};

// One fuzzer execution.
inline void
fuzzExtProcRun(const test::extensions::filters::http::ext_proc::ExtProcGrpcTestCase& input,
               const bool persistent_mode) {
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
  static std::unique_ptr<std::string> ext_proc_data = nullptr;
  ext_proc_data = std::make_unique<std::string>(input.ext_proc_data());
  static std::unique_ptr<FuzzedDataProvider> ext_proc_provider = nullptr;
  ext_proc_provider = std::make_unique<FuzzedDataProvider>(
      reinterpret_cast<const uint8_t*>(ext_proc_data->data()), ext_proc_data->size());

  static std::unique_ptr<ExtProcIntegrationFuzz> fuzzer = nullptr;
  static std::unique_ptr<ExtProcFuzzHelper> fuzz_helper = nullptr;

  static uint32_t fuzz_exec_count = 0;
  // Initialize fuzzer with IP and gRPC version from environment
  if (fuzzCreateEnvoy(fuzz_exec_count, persistent_mode)) {
    fuzzer = std::make_unique<ExtProcIntegrationFuzz>(
        TestEnvironment::getIpVersionsForTest()[0], TestEnvironment::getsGrpcVersionsForTest()[0]);
  }
  // Initialize fuzz_helper.
  fuzz_helper = std::make_unique<ExtProcFuzzHelper>(ext_proc_provider.get());

  // Initialize test server.
  if (fuzzCreateEnvoy(fuzz_exec_count, persistent_mode)) {
    // This starts an external processor in a separate thread. This allows for the
    // external process to consume messages in a loop without blocking the fuzz
    // target from receiving the response.
    fuzzer->test_processor_.start(
        fuzzer->ip_version_,
        [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
          while (true) {
            ProcessingRequest req;
            if (!stream->Read(&req)) {
              return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
            }
            bool immediate_close_grpc = false;
            ProcessingResponse resp;
            auto result = fuzz_helper->generateResponse(req, resp, immediate_close_grpc);
            if (immediate_close_grpc) {
              return result;
            }
            stream->Write(resp);
          }
          return grpc::Status::OK;
        });
  }
  // Initialize Envoy
  if (fuzzCreateEnvoy(fuzz_exec_count, persistent_mode)) {
    fuzzer->initializeFuzzer(true);
    ENVOY_LOG_MISC(trace, "Fuzzer initialized");
  }

  const auto response = fuzzer->randomDownstreamRequest(&downstream_provider);
  // For fuzz testing we don't care about the response code, only that
  // the stream ended in some graceful manner
  ENVOY_LOG_MISC(trace, "Waiting for response.");

  bool response_timeout = false;
  if (response->waitForEndStream(std::chrono::milliseconds(500))) {
    ENVOY_LOG_MISC(trace, "Response received.");
  } else {
    response_timeout = true;
    ENVOY_LOG_MISC(trace, "Response timed out.");
  }

  fuzz_exec_count++;
  if (fuzzCreateEnvoy(fuzz_exec_count, persistent_mode) || response_timeout) {
    fuzzer->tearDown(false);
    fuzzer.reset();
    fuzz_exec_count = 0;
  } else {
    fuzzer->tearDown(true);
  }
  ext_proc_data.reset();
  ext_proc_provider.reset();
  fuzz_helper.reset();
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
