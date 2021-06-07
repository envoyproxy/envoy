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

using ProcessingFunc = std::function<grpc::Status(
    grpc::ServerReaderWriter<envoy::service::ext_proc::v3alpha::ProcessingResponse,
                             envoy::service::ext_proc::v3alpha::ProcessingRequest>*)>;

class ProcessorWrapper : public envoy::service::ext_proc::v3alpha::ExternalProcessor::Service {
public:
  ProcessorWrapper(ProcessingFunc& cb) : callback_(cb) {}

  grpc::Status
  Process(grpc::ServerContext*,
          grpc::ServerReaderWriter<envoy::service::ext_proc::v3alpha::ProcessingResponse,
                                   envoy::service::ext_proc::v3alpha::ProcessingRequest>* stream)
      override {
    return callback_(stream);
  }

private:
  ProcessingFunc callback_;
};

class TestProcessor {
public:
  // Start the processor listening on an ephemeral port (port 0) on 127.0.0.1.
  // All new streams will be delegated to the specified function.
  void start(ProcessingFunc cb);

  // Stop the processor from listening once all streams are closed.
  void shutdown();

  // Return the port that the processor is listening on.
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
