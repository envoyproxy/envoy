#pragma once

#include <memory>
#include <string>

#include "source/common/http/sidestream_watermark.h"
#include "source/extensions/filters/common/ext_proc/client_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExternalProcessing {

/**
 * Callbacks used by the processor stream.
 */
template <typename ResponseType> class ProcessorCallbacks : public RequestCallbacks<ResponseType> {
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
template <typename RequestType, typename ResponseType> class ProcessorStream : public StreamBase {
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
  start(ProcessorCallbacks<ResponseType>& callbacks,
        const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
        Http::AsyncClient::StreamOptions& options,
        Http::StreamFilterSidestreamWatermarkCallbacks& sidestream_watermark_callbacks) PURE;
};

template <typename RequestType, typename ResponseType>
using ProcessorClientPtr = std::unique_ptr<ProcessorClient<RequestType, ResponseType>>;

} // namespace ExternalProcessing
} // namespace Common
} // namespace Extensions
} // namespace Envoy
