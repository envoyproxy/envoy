#pragma once

#include <chrono>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/grpc/status.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExternalProcessorStream {
public:
  virtual ~ExternalProcessorStream() = default;
  virtual void send(envoy::service::ext_proc::v3alpha::ProcessingRequest&& request,
                    bool end_stream) PURE;
  virtual void close() PURE;
};

using ExternalProcessorStreamPtr = std::unique_ptr<ExternalProcessorStream>;

class ExternalProcessorCallbacks {
public:
  virtual ~ExternalProcessorCallbacks() = default;
  virtual void onReceiveMessage(
      std::unique_ptr<envoy::service::ext_proc::v3alpha::ProcessingResponse>&& response) PURE;
  virtual void onGrpcError(Grpc::Status::GrpcStatus error) PURE;
  virtual void onGrpcClose() PURE;
};

class ExternalProcessorClient {
public:
  virtual ~ExternalProcessorClient() = default;
  virtual ExternalProcessorStreamPtr start(ExternalProcessorCallbacks& callbacks,
                                           const std::chrono::milliseconds& timeout) PURE;
};

using ExternalProcessorClientPtr = std::unique_ptr<ExternalProcessorClient>;

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
