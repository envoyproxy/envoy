#pragma once

#include <memory>

#include "envoy/common/time.h"
#include "envoy/grpc/status.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "gtest/gtest.h"

namespace Envoy {

class FakeStream;

namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Controls ext_proc responses on the integration test's FakeUpstream/FakeStream transport and waits
// for Envoy to finish handling each response, without advancing simulated time. A successful write
// to the fake server's stream does not guarantee that Envoy has applied the response yet.
//
// Construction installs a test filter factory that observes the real gRPC client's callbacks. This
// helper must be constructed before initializing Envoy; destruction restores the original factory
// registrations. The time source must be the simulated clock used by the test.
class ExtProcTestServer {
public:
  explicit ExtProcTestServer(TimeSource& time_source);
  ~ExtProcTestServer();

  testing::AssertionResult
  sendResponse(FakeStream& stream,
               const envoy::service::ext_proc::v3::ProcessingResponse& response);
  testing::AssertionResult startStream(FakeStream& stream, bool send_headers = true);
  testing::AssertionResult finishStream(FakeStream& stream, Grpc::Status::GrpcStatus status);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
