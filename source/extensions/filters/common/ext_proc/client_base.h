#pragma once

#include <memory>

#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace ExtProc {

// Forward declarations for templated classes
template <typename RequestType, typename ResponseType>
class RequestCallbacks;

template <typename RequestType, typename ResponseType>
class StreamBase;

template <typename RequestType, typename ResponseType>
class ClientBase;

/**
 * Async callbacks used during external processing.
 */
template <typename RequestType, typename ResponseType>
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;
  virtual void onComplete(ResponseType& response) PURE;
  virtual void onError() PURE;
};

/**
 * Stream base class used during external processing.
 */
template <typename RequestType, typename ResponseType>
class StreamBase {
public:
  virtual ~StreamBase() = default;
};

/**
 * Async client base class used during external processing.
 */
template <typename RequestType, typename ResponseType>
class ClientBase {
public:
  virtual ~ClientBase() = default;
  virtual void sendRequest(RequestType&& request, bool end_stream, const uint64_t stream_id,
                           RequestCallbacks<RequestType, ResponseType>* callbacks,
                           StreamBase<RequestType, ResponseType>* stream) PURE;
  virtual void cancel() PURE;
  virtual const Envoy::StreamInfo::StreamInfo* getStreamInfo() const PURE;
};

template <typename RequestType, typename ResponseType>
using ClientBasePtr = std::unique_ptr<ClientBase<RequestType, ResponseType>>;

} // namespace ExtProc
} // namespace Common
} // namespace Extensions
} // namespace Envoy
