#pragma once

#include <memory>

#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExternalProcessing {

/**
 * Stream base class used during external processing.
 */
template <typename RequestType, typename ResponseType> class StreamBase {
public:
  virtual ~StreamBase() = default;
  virtual void send(RequestType&& request, bool end_stream) PURE;
  virtual bool close() PURE;
  virtual bool halfCloseAndDeleteOnRemoteClose() PURE;
  virtual const StreamInfo::StreamInfo& streamInfo() const PURE;
  virtual StreamInfo::StreamInfo& streamInfo() PURE;
  virtual void notifyFilterDestroy() PURE;
};

/**
 * Async callbacks used during external processing.
 */
template <typename ResponseType> class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;
  virtual void onComplete(ResponseType& response) PURE;
  virtual void onError() PURE;
};

/**
 * Async client base class used during external processing.
 */
template <typename RequestType, typename ResponseType> class ClientBase {
public:
  virtual ~ClientBase() = default;
  virtual void sendRequest(RequestType&& request, bool end_stream, const uint64_t stream_id,
                           RequestCallbacks<ResponseType>* callbacks,
                           StreamBase<RequestType, ResponseType>* stream) PURE;
  virtual void cancel() PURE;
  virtual const Envoy::StreamInfo::StreamInfo* getStreamInfo() const PURE;
};

template <typename RequestType, typename ResponseType>
using ClientBasePtr = std::unique_ptr<ClientBase<RequestType, ResponseType>>;

} // namespace ExternalProcessing
} // namespace Common
} // namespace Extensions
} // namespace Envoy
