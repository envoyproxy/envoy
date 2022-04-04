#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/ext_proc/client.h"

using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using ProcessingResponsePtr = std::unique_ptr<ProcessingResponse>;

class ExternalProcessorClientImpl : public ExternalProcessorClient {
public:
  ExternalProcessorClientImpl(Grpc::AsyncClientManager& client_manager,
                              const envoy::config::core::v3::GrpcService& grpc_service,
                              Stats::Scope& scope);

  ExternalProcessorStreamPtr start(ExternalProcessorCallbacks& callbacks,
                                   const StreamInfo::StreamInfo& stream_info) override;

private:
  Grpc::AsyncClientManager& client_manager_;
  const envoy::config::core::v3::GrpcService grpc_service_;
  Stats::Scope& scope_;
};

class ExternalProcessorStreamImpl : public ExternalProcessorStream,
                                    public Grpc::AsyncStreamCallbacks<ProcessingResponse>,
                                    public Logger::Loggable<Logger::Id::ext_proc> {
public:
  ExternalProcessorStreamImpl(Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>&& client,
                              ExternalProcessorCallbacks& callbacks,
                              const StreamInfo::StreamInfo& stream_info);
  void send(ProcessingRequest&& request, bool end_stream) override;
  // Close the stream. This is idempotent and will return true if we
  // actually closed it.
  bool close() override;

  // AsyncStreamCallbacks
  void onReceiveMessage(ProcessingResponsePtr&& message) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

private:
  ExternalProcessorCallbacks& callbacks_;
  Grpc::AsyncClient<ProcessingRequest, ProcessingResponse> client_;
  Grpc::AsyncStream<ProcessingRequest> stream_;
  Http::AsyncClient::ParentContext grpc_context_;
  bool stream_closed_ = false;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
