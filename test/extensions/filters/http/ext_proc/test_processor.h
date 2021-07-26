#pragma once

#include <functional>
#include <memory>

#include "envoy/service/ext_proc/v3alpha/external_processor.grpc.pb.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

#include "grpc++/server.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Implementations of this function are called for each gRPC stream sent
// to the external processing server.
using ProcessingFunc = std::function<void(
    grpc::ServerReaderWriter<envoy::service::ext_proc::v3alpha::ProcessingResponse,
                             envoy::service::ext_proc::v3alpha::ProcessingRequest>*)>;

// An implementation of the ExternalProcessor service that may be included
// in integration tests.
class ProcessorWrapper : public envoy::service::ext_proc::v3alpha::ExternalProcessor::Service {
public:
  ProcessorWrapper(ProcessingFunc& cb) : callback_(cb) {}

  grpc::Status
  Process(grpc::ServerContext*,
          grpc::ServerReaderWriter<envoy::service::ext_proc::v3alpha::ProcessingResponse,
                                   envoy::service::ext_proc::v3alpha::ProcessingRequest>* stream)
      override;

private:
  ProcessingFunc callback_;
};

// This class starts a gRPC server supporting the ExternalProcessor service.
// It delegates each gRPC stream to a method that can process the stream and
// use ASSERT_ and EXPECT_ macros to validate test results.
class TestProcessor {
public:
  // Start the processor listening on an ephemeral port (port 0) on 127.0.0.1.
  // All new streams will be delegated to the specified function. The function
  // will be invoked in a background thread controlled by the gRPC server.
  void start(ProcessingFunc cb);

  // Stop the processor from listening once all streams are closed, and exit
  // the listening threads.
  void shutdown();

  // Return the port that the processor is listening on from the call to
  // "start".
  int port() const { return listening_port_; }

private:
  std::unique_ptr<ProcessorWrapper> wrapper_;
  std::unique_ptr<grpc::Server> server_;
  int listening_port_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
