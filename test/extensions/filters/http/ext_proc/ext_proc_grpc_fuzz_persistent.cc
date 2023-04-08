#include "test/extensions/filters/http/ext_proc/ext_proc_grpc_fuzz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

namespace {
// Reset and recreate Envoy and test server after every 2000 fuzzer execs.
bool fuzzCreateEnvoy(const uint32_t exec) {
  const uint32_t exec_before_reset_envoy = 2000;
  return (exec % exec_before_reset_envoy == 0);
}
} // namespace

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

  // Using persistent Envoy and ext_proc test server.
  static std::unique_ptr<ExtProcIntegrationFuzz> fuzzer = nullptr;
  static std::unique_ptr<ExtProcFuzzHelper> fuzz_helper = nullptr;
  // Protects fuzz_helper which will be accessed by multiple threads.
  static Thread::MutexBasicLockable fuzz_helper_lock_;

  static uint32_t fuzz_exec_count = 0;
  // Initialize fuzzer once with IP and gRPC version from environment
  if (fuzzCreateEnvoy(fuzz_exec_count)) {
    fuzzer = std::make_unique<ExtProcIntegrationFuzz>(
        TestEnvironment::getIpVersionsForTest()[0], TestEnvironment::getsGrpcVersionsForTest()[0]);
  }
  // Initialize fuzz_helper during every execution.
  // This will be accessed by the test server which is initialized once.
  fuzz_helper = std::make_unique<ExtProcFuzzHelper>(&ext_proc_provider);

  // Initialize test server.
  if (fuzzCreateEnvoy(fuzz_exec_count)) {
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

            fuzz_helper_lock_.lock();
            if (fuzz_helper != nullptr) {
              fuzz_helper->logRequest(&req);
              // The following blocks generate random data for the 9 fields of the
              // ProcessingResponse gRPC message

              // 1 - 7. Randomize response
              // If true, immediately close the connection with a random Grpc Status.
              // Otherwise randomize the response
              ProcessingResponse resp;
              if (fuzz_helper->provider_->ConsumeBool()) {
                ENVOY_LOG_MISC(trace, "Immediately Closing gRPC connection");
                auto result = fuzz_helper->randomGrpcStatusWithMessage();
                fuzz_helper_lock_.unlock();
                return result;
              } else {
                ENVOY_LOG_MISC(trace, "Generating Random ProcessingResponse");
                fuzz_helper->randomizeResponse(&resp, &req);
              }

              // 8. Randomize dynamic_metadata
              // TODO(ikepolinsky): ext_proc does not support dynamic_metadata

              // 9. Randomize mode_override
              if (fuzz_helper->provider_->ConsumeBool()) {
                ENVOY_LOG_MISC(trace, "Generating Random ProcessingMode Override");
                ProcessingMode* msg = resp.mutable_mode_override();
                fuzz_helper->randomizeOverrideResponse(msg);
              }
              ENVOY_LOG_MISC(trace, "Response generated, writing to stream.");
              stream->Write(resp);
            }
            fuzz_helper_lock_.unlock();
          }
          return grpc::Status::OK;
        });
  }
  // Initialize Envoy
  if (fuzzCreateEnvoy(fuzz_exec_count)) {
    fuzzer->initializeFuzzer(true);
    ENVOY_LOG_MISC(trace, "Fuzzer initialized");
  }

  const auto response = fuzzer->randomDownstreamRequest(&downstream_provider);
  // For fuzz testing we don't care about the response code, only that
  // the stream ended in some graceful manner
  ENVOY_LOG_MISC(trace, "Waiting for response.");

  if (response->waitForEndStream(std::chrono::milliseconds(500))) {
    ENVOY_LOG_MISC(trace, "Response received.");
  } else {
    ENVOY_LOG_MISC(trace, "Response timed out.");
  }

  fuzz_exec_count++;
  if (fuzzCreateEnvoy(fuzz_exec_count)) {
    fuzzer->tearDown(false);
    fuzzer.reset();
    fuzzer = nullptr;
  } else {
    fuzzer->tearDown(true);
  }
  // Protect fuzz_helper before reset since it is used in the test server.
  fuzz_helper_lock_.lock();
  fuzz_helper.reset();
  fuzz_helper = nullptr;
  fuzz_helper_lock_.unlock();
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
