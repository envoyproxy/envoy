#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "source/common/network/address_impl.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/test_processor.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz_helper.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::BodyResponse;
using envoy::service::ext_proc::v3alpha::CommonResponse;
using envoy::service::ext_proc::v3alpha::HeadersResponse;
using envoy::service::ext_proc::v3alpha::HttpBody;
using envoy::service::ext_proc::v3alpha::HttpHeaders;
using envoy::service::ext_proc::v3alpha::HttpTrailers;
using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;
using envoy::service::ext_proc::v3alpha::TrailersResponse;
using envoy::type::v3::StatusCode;
using envoy::config::core::v3::HeaderValueOption;
using envoy::config::core::v3::HeaderValue;


using Http::LowerCaseString;

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

  void TearDown() {
    cleanupUpstreamAndDownstream();
    test_processor_.shutdown();
  }

  Network::Address::IpVersion ipVersion() const override { return ip_version_; }
  Grpc::ClientType clientType() const override { return client_type_; }

  void initializeFuzzer(bool autonomous_upstream) {
    autonomous_upstream_ = autonomous_upstream;
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

  IntegrationStreamDecoderPtr sendDownstreamRequest(
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      const std::string http_method = "GET") {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{{":method", http_method}};
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    HttpTestUtility::addDefaultHeaders(headers, false);
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithBody(
      absl::string_view body,
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      const std::string http_method = "POST") {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{{":method", http_method}};
    HttpTestUtility::addDefaultHeaders(headers, false);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    return codec_client_->makeRequestWithBody(headers, std::string(body));
  }

  IntegrationStreamDecoderPtr sendDownstreamRequestWithChunks(
      FuzzedDataProvider* fdp,
      absl::optional<std::function<void(Http::HeaderMap& headers)>> modify_headers,
      const std::string http_method = "POST") {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{{":method", http_method}};
    HttpTestUtility::addDefaultHeaders(headers, false);
    if (modify_headers) {
      (*modify_headers)(headers);
    }
    auto encoder_decoder = codec_client_->startRequest(headers);
    IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
    auto& encoder = encoder_decoder.first;

    uint32_t num_chunks = fdp->ConsumeIntegralInRange<uint32_t>(0, 50);
    for (uint32_t i = 0; i < num_chunks; i++) {
      Buffer::OwnedImpl chunk;
      std::string data = fdp->ConsumeRandomLengthString();
      chunk.add(data);
      ENVOY_LOG_MISC(trace, "Sending chunk of {} bytes", data.length());
      codec_client_->sendData(encoder, chunk, false);
    }

    Buffer::OwnedImpl empty_chunk;
    codec_client_->sendData(encoder, empty_chunk, true);
    return response;
  }

  IntegrationStreamDecoderPtr randomDownstreamRequest(FuzzedDataProvider* fdp) {
    // From the external processor's view each of these requests
    // are handled the same way. They only differ in what the server should
    // send back to the client.
    switch (fdp->ConsumeEnum<HttpMethod>()) {
      case HttpMethod::GET:
        ENVOY_LOG_MISC(trace, "Sending GET request");
        return sendDownstreamRequest(absl::nullopt);
      case HttpMethod::POST:
        if (fdp->ConsumeBool()) {
          ENVOY_LOG_MISC(trace, "Sending POST request with body");
          return sendDownstreamRequestWithBody(fdp->ConsumeRandomLengthString(), absl::nullopt);
        } else {
          ENVOY_LOG_MISC(trace, "Sending POST request with chunked body");
          return sendDownstreamRequestWithChunks(fdp, absl::nullopt);
        }
      default:
        ENVOY_LOG_MISC(error, "Unhandled HttpMethod");
        exit(EXIT_FAILURE);
    }
  }

  envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor proto_config_{};
  TestProcessor test_processor_;
  Network::Address::IpVersion ip_version_;
  Grpc::ClientType client_type_;
};

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);

  ExtProcIntegrationFuzz fuzzer(Network::Address::IpVersion::v4, Grpc::ClientType::GoogleGrpc);
  fuzzer.test_processor_.start(
      [&provider](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        while (true) {
          ProcessingRequest req;
          if (!stream->Read(&req)) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "expected message");
          }

          // 1 - 7. Randomize response
          // If true, immediately close the connection with a random Grpc Status.
          // Otherwise randomize the response
          ProcessingResponse resp;
          if (provider.ConsumeBool()) {
            return RandomGrpcStatusWithMessage(&provider);
          } else {
            RandomizeResponse(&resp, &req, &provider);
          }

          // 8. Randomize dynamic_metadata
          /* TODO: Skipping - not implemented
          if (provider.ConsumeBool()) {
          } */

          // 9. Randomize mode_override
          if (provider.ConsumeBool()) {
            ProcessingMode* msg = resp.mutable_mode_override();
            RandomizeOverrideResponse(msg, &provider);
           }

          ENVOY_LOG_MISC(trace, "Response generated, writing to stream.");
          stream->Write(resp);
        }

        return grpc::Status::OK;
      });

  ENVOY_LOG_MISC(trace, "External Process started.");

  fuzzer.initializeFuzzer(true);
  ENVOY_LOG_MISC(trace, "Fuzzer initialized");

  auto response = fuzzer.randomDownstreamRequest(&provider);

  // For fuzz testing we don't care about the response code, only that
  // the stream ended in some graceful manner
  ENVOY_LOG_MISC(trace, "Waiting for response.");
  if (response->waitForEndStream(std::chrono::milliseconds(2000))) {
    ENVOY_LOG_MISC(trace, "Response received.");
  } else {
    // TODO: investigate if there is anyway around this. Waiting 2 seconds
    // for a fuzz case to fail will drasticially cut executions/second.
    ENVOY_LOG_MISC(trace, "Response timed out.");
  }
  fuzzer.TearDown();
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
