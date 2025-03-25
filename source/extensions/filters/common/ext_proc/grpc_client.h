#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/common/ext_proc/client_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExtProc {

/**
 * Callbacks used by the processor stream.
 */
template <typename RequestType, typename ResponseType>
class ProcessorCallbacks {
public:
  virtual ~ProcessorCallbacks() = default;
  virtual void onReceiveMessage(std::unique_ptr<ResponseType>&& response) PURE;
  virtual void onGrpcError(Grpc::Status::GrpcStatus error, const std::string& message) PURE;
  virtual void onGrpcClose() PURE;
  virtual void logStreamInfo() PURE;
};

/**
 * A stream to an external processing server. This uses a bidirectional gRPC stream to send
 * requests and receive responses.
 */
template <typename RequestType, typename ResponseType>
class ProcessorStream : public StreamBase<RequestType, ResponseType> {
public:
  ~ProcessorStream() override = default;
  virtual void send(RequestType&& request, bool end_stream) PURE;
  // Idempotent close. Return true if it actually closed.
  virtual bool close() PURE;
  virtual bool halfCloseAndDeleteOnRemoteClose() PURE;
  virtual const StreamInfo::StreamInfo& streamInfo() const PURE;
  virtual StreamInfo::StreamInfo& streamInfo() PURE;
  virtual void notifyFilterDestroy() PURE;
};

template <typename RequestType, typename ResponseType>
using ProcessorStreamPtr = std::unique_ptr<ProcessorStream<RequestType, ResponseType>>;

/**
 * A client for an external processing server.
 */
template <typename RequestType, typename ResponseType>
class ProcessorClient : public ClientBase<RequestType, ResponseType> {
public:
  virtual ~ProcessorClient() = default;
  virtual ProcessorStreamPtr<RequestType, ResponseType>
  start(ProcessorCallbacks<RequestType, ResponseType>& callbacks,
        const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
        Http::AsyncClient::StreamOptions& options,
        Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) PURE;
};

template <typename RequestType, typename ResponseType>
using ProcessorClientPtr = std::unique_ptr<ProcessorClient<RequestType, ResponseType>>;

/**
 * Implementation of ProcessorClient that uses a gRPC service.
 */
template <typename RequestType, typename ResponseType>
class ProcessorClientImpl : public ProcessorClient<RequestType, ResponseType> {
public:
  ProcessorClientImpl(Grpc::AsyncClientManager& client_manager, Stats::Scope& scope,
                       const std::string& service_method)
      : client_manager_(client_manager), scope_(scope), service_method_(service_method) {}

  ProcessorStreamPtr<RequestType, ResponseType>
  start(ProcessorCallbacks<RequestType, ResponseType>& callbacks,
        const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
        Http::AsyncClient::StreamOptions& options,
        Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) override;

  void sendRequest(RequestType&& request, bool end_stream, const uint64_t stream_id,
                   RequestCallbacks<RequestType, ResponseType>* callbacks,
                   StreamBase<RequestType, ResponseType>* stream) override;
  
  void cancel() override {}
  const Envoy::StreamInfo::StreamInfo* getStreamInfo() const override { return nullptr; }

private:
  Grpc::AsyncClientManager& client_manager_;
  Stats::Scope& scope_;
  const std::string service_method_;
};

} // namespace ExtProc
} // namespace Common
} // namespace Extensions
} // namespace Envoy
