#pragma once

#include <chrono>

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/grpc/status.h"
#include "envoy/http/header_map.h"
#include "envoy/tracing/http_tracer.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Grpc {

/**
 * An in-flight gRPC unary RPC.
 */
class AsyncRequest {
public:
  virtual ~AsyncRequest() {}

  /**
   * Signals that the request should be cancelled. No further callbacks will be invoked.
   */
  virtual void cancel() PURE;
};

/**
 * An in-flight gRPC stream.
 */
template <class RequestType> class AsyncStream {
public:
  virtual ~AsyncStream() {}

  /**
   * Send request message to the stream.
   * @param request protobuf serializable message.
   * @param end_stream close the stream locally. No further methods may be invoked on the stream
   *                   object, but callbacks may still be received until the stream is closed
   *                   remotely.
   */
  virtual void sendMessage(const RequestType& request, bool end_stream) PURE;

  /**
   * Close the stream locally and send an empty DATA frame to the remote. No further methods may be
   * invoked on the stream object, but callbacks may still be received until the stream is closed
   * remotely.
   */
  virtual void closeStream() PURE;

  /**
   * Close the stream locally and remotely (as needed). No further methods may be invoked on the
   * stream object and no further callbacks will be invoked.
   */
  virtual void resetStream() PURE;
};

/**
 * Factory to create tracing span decorators for gRPC requests.
 */
template <class RequestType, class ResponseType> class AsyncSpanFinalizerFactory {
public:
  virtual ~AsyncSpanFinalizerFactory() {}

  virtual Tracing::SpanFinalizerPtr create(const RequestType& request,
                                           const ResponseType* response) PURE;
};

template <class ResponseType> class AsyncRequestCallbacks {
public:
  virtual ~AsyncRequestCallbacks() {}

  /**
   * Called when populating the headers to send with initial metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onCreateInitialMetadata(Http::HeaderMap& metadata) PURE;

  /**
   * Called when the async gRPC request succeeds. No further callbacks will be invoked.
   * @param response the gRPC response.
   */
  virtual void onSuccess(std::unique_ptr<ResponseType>&& response) PURE;

  /**
   * Called when the async gRPC request fails. No further callbacks will be invoked.
   * @param status the gRPC status.
   * @param message the gRPC status message or empty string if not present.
   */
  virtual void onFailure(Status::GrpcStatus status, const std::string& message) PURE;
};

/**
 * Notifies caller of async gRPC stream status.
 * Note the gRPC stream is full-duplex, even if the local to remote stream has been ended by
 * AsyncStream.close(), AsyncStreamCallbacks can continue to receive events until the remote
 * to local stream is closed (onRemoteClose), and vice versa. Once the stream is closed remotely, no
 * further callbacks will be invoked.
 */
template <class ResponseType> class AsyncStreamCallbacks {
public:
  virtual ~AsyncStreamCallbacks() {}

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
   * @param message the gRPC status message or empty string if not present.
   */
  virtual void onRemoteClose(Status::GrpcStatus status, const std::string& message) PURE;
};

/**
 * Supports sending gRPC requests and receiving responses asynchronously. This can be used to
 * implement either plain gRPC or streaming gRPC calls.
 */
template <class RequestType, class ResponseType> class AsyncClient {
public:
  virtual ~AsyncClient() {}

  /**
   * Start a gRPC unary RPC asynchronously.
   * @param service_method protobuf descriptor of gRPC service method.
   * @param request protobuf serializable message.
   * @param callbacks the callbacks to be notified of RPC status.
   * @param parent_span the current parent tracing context.
   * @param timeout supplies the request timeout.
   * @return a request handle or nullptr if no request could be started. NOTE: In this case
   *         onFailure() has already been called inline. The client owns the request and the
   *         handle should just be used to cancel.
   */
  virtual AsyncRequest*
  send(const Protobuf::MethodDescriptor& service_method, const RequestType& request,
       AsyncRequestCallbacks<ResponseType>& callbacks, Tracing::Span& parent_span,
       AsyncSpanFinalizerFactory<RequestType, ResponseType>& finalizer_factory,
       const Optional<std::chrono::milliseconds>& timeout) PURE;

  /**
   * Start a gRPC stream asynchronously.
   * @param service_method protobuf descriptor of gRPC service method.
   * @param callbacks the callbacks to be notified of stream status.
   * @return a stream handle or nullptr if no stream could be started. NOTE: In this case
   *         onRemoteClose() has already been called inline. The client owns the stream and
   *         the handle can be used to send more messages or finish the stream. It is expected that
   *         closeStream() is invoked by the caller to notify the client that the stream resources
   *         may be reclaimed.
   */
  virtual AsyncStream<RequestType>* start(const Protobuf::MethodDescriptor& service_method,
                                          AsyncStreamCallbacks<ResponseType>& callbacks) PURE;
};

} // namespace Grpc
} // namespace Envoy
