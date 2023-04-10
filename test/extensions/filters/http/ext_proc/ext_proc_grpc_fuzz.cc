#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

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
          bool immediate_close_grpc = false;
          ProcessingResponse resp;
          auto result = fuzz_helper.generateResponse(req, resp, immediate_close_grpc);
          if (immediate_close_grpc) {
            return result;
          }
          stream->Write(resp);
        }
        return grpc::Status::OK;
      });

  ENVOY_LOG_MISC(trace, "External Process started.");

  fuzzer.initializeFuzzer(true);
  ENVOY_LOG_MISC(trace, "Fuzzer initialized");

  const auto response = fuzzer.randomDownstreamRequest(&downstream_provider);

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
  fuzzer.tearDown(false);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
