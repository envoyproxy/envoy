#pragma once

#include <chrono>

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/grpc/status.h"
#include "envoy/http/header_map.h"

#include "google/protobuf/descriptor.h"

namespace Envoy {
namespace Grpc {

/**
 * An in-flight gRPC stream.
 */
template <class RequestType> class AsyncClientStream {
public:
  virtual ~AsyncClientStream() {}

  /**
   * Send request message to the stream.
   * @param request protobuf serializable message.
   */
  virtual void sendMessage(const RequestType& request) PURE;

  /**
   * Close the stream locally. No further methods may be invoked on the
   * stream object, but callbacks may still be received until the stream is closed remotely.
   */
  virtual void closeStream() PURE;

  /**
   * Close the stream locally and remotely (as needed). No further methods may be invoked on the
   * stream object and no further callbacks will be invoked.
   */
  virtual void resetStream() PURE;
};

/**
 * Notifies caller of async gRPC stream status.
 * Note the gRPC stream is full-duplex, even if the local to remote stream has been ended by
 * AsyncClientStream.close(), AsyncClientCallbacks can continue to receive events until the remote
 * to local stream is closed (onRemoteClose), and vice versa. Once the stream is closed remotely, no
 * further callbacks will be invoked.
 */
template <class ResponseType> class AsyncClientCallbacks {
public:
  virtual ~AsyncClientCallbacks() {}

  /**
   * Called when populating the headers to send with initial metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onCreateInitialMetadata(Http::HeaderMap& metadata) PURE;

  /**
   * Called when initial metadata is recevied.
   * @param metadata initial metadata reference.
   */
  virtual void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) PURE;

  /**
   * Called when an async gRPC message is received.
   * @param response the gRPC message.
   */
  virtual void onReceiveMessage(std::unique_ptr<ResponseType>&& message) PURE;

  /**
   * Called when trailing metadata is recevied.
   * @param metadata trailing metadata reference.
   */
  virtual void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) PURE;

  /**
   * Called when the remote closes or an error occurs on the gRPC stream. The stream is
   * considered remotely closed after this invocation and no further callbacks will be
   * invoked. A non-Ok status implies that stream is also locally closed and that no
   * further stream operations are permitted.
   * @param status the gRPC status.
   */
  virtual void onRemoteClose(Status::GrpcStatus status) PURE;
};

/**
 * Supports sending gRPC requests and receiving responses asynchronously. This can be used to
 * implement either plain gRPC or streaming gRPC calls.
 */
template <class RequestType, class ResponseType> class AsyncClient {
public:
  virtual ~AsyncClient() {}

  /**
   * Start a gRPC stream asynchronously.
   * @param service_method protobuf descriptor of gRPC service method.
   * @param callbacks the callbacks to be notified of stream status.
   * @param timeout supplies the stream timeout, measured since when the frame with end_stream
   *        flag is sent until when the first frame is received.
   * @return a stream handle or nullptr if no stream could be started. NOTE: In this case
   *         onRemoteClose() has already been called inline. The client owns the stream and
   *         the handle can be used to send more messages or finish the stream. It is expected that
   *         finish() is invoked by the caller to notify the client that the stream resources may
   *         be reclaimed.
   */
  virtual AsyncClientStream<RequestType>*
  start(const google::protobuf::MethodDescriptor& service_method,
        AsyncClientCallbacks<ResponseType>& callbacks,
        const Optional<std::chrono::milliseconds>& timeout) PURE;
};

} // namespace Grpc
} // namespace Envoy
