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
class AsyncStream {
public:
  virtual ~AsyncStream() {}

  /**
   * Send request message to the stream.
   * @param request protobuf serializable message.
   * @param end_stream close the stream locally. No further methods may be invoked on the stream
   *                   object, but callbacks may still be received until the stream is closed
   *                   remotely.
   */
  virtual void sendMessage(const Protobuf::Message& request, bool end_stream) PURE;

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

class AsyncRequestCallbacks {
public:
  virtual ~AsyncRequestCallbacks() {}

  /**
   * Called when populating the headers to send with initial metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onCreateInitialMetadata(Http::HeaderMap& metadata) PURE;

  /**
   * Factory for empty response messages.
   * @return ProtobufTypes::MessagePtr a Protobuf::Message with the response
   *         type for the request.
   */
  virtual ProtobufTypes::MessagePtr createEmptyResponse() PURE;

  /**
   * Called when the async gRPC request succeeds. No further callbacks will be invoked.
   * @param response the gRPC response.
   * @param span a tracing span to fill with extra tags.
   */
  virtual void onSuccessUntyped(ProtobufTypes::MessagePtr&& response, Tracing::Span& span) PURE;

  /**
   * Called when the async gRPC request fails. No further callbacks will be invoked.
   * @param status the gRPC status.
   * @param message the gRPC status message or empty string if not present.
   * @param span a tracing span to fill with extra tags.
   */
  virtual void onFailure(Status::GrpcStatus status, const std::string& message,
                         Tracing::Span& span) PURE;
};

// Templatized variant of AsyncRequestCallbacks.
template <class ResponseType> class TypedAsyncRequestCallbacks : public AsyncRequestCallbacks {
public:
  ProtobufTypes::MessagePtr createEmptyResponse() override {
    return std::make_unique<ResponseType>();
  }

  virtual void onSuccess(std::unique_ptr<ResponseType>&& response, Tracing::Span& span) PURE;

  void onSuccessUntyped(ProtobufTypes::MessagePtr&& response, Tracing::Span& span) override {
    onSuccess(std::unique_ptr<ResponseType>(dynamic_cast<ResponseType*>(response.release())), span);
  }
};

/**
 * Notifies caller of async gRPC stream status.
 * Note the gRPC stream is full-duplex, even if the local to remote stream has been ended by
 * AsyncStream.close(), AsyncStreamCallbacks can continue to receive events until the remote
 * to local stream is closed (onRemoteClose), and vice versa. Once the stream is closed remotely, no
 * further callbacks will be invoked.
 */
class AsyncStreamCallbacks {
public:
  virtual ~AsyncStreamCallbacks() {}

  /**
   * Factory for empty response messages.
   * @return ProtobufTypes::MessagePtr a Protobuf::Message with the response
   *          type for the stream.
   */
  virtual ProtobufTypes::MessagePtr createEmptyResponse() PURE;

  /**
   * Called when populating the headers to send with initial metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onCreateInitialMetadata(Http::HeaderMap& metadata) PURE;

  /**
   * Called when initial metadata is received. This will be called with empty metadata on a
   * trailers-only response, followed by onReceiveTrailingMetadata() with the trailing metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) PURE;

  /**
   * Called when an async gRPC message is received.
   * @param response the gRPC message.
   */
  virtual void onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& message) PURE;

  /**
   * Called when trailing metadata is recevied. This will also be called on non-Ok grpc-status
   * stream termination.
   * @param metadata trailing metadata reference.
   */
  virtual void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) PURE;

  /**
   * Called when the remote closes or an error occurs on the gRPC stream. The stream is
   * considered remotely closed after this invocation and no further callbacks will be
   * invoked. In addition, no further stream operations are permitted.
   * @param status the gRPC status.
   * @param message the gRPC status message or empty string if not present.
   */
  virtual void onRemoteClose(Status::GrpcStatus status, const std::string& message) PURE;
};

// Templatized variant of AsyncStreamCallbacks.
template <class ResponseType> class TypedAsyncStreamCallbacks : public AsyncStreamCallbacks {
public:
  ProtobufTypes::MessagePtr createEmptyResponse() override {
    return std::make_unique<ResponseType>();
  }

  virtual void onReceiveMessage(std::unique_ptr<ResponseType>&& message) PURE;

  void onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& message) override {
    onReceiveMessage(std::unique_ptr<ResponseType>(dynamic_cast<ResponseType*>(message.release())));
  }
};

/**
 * Supports sending gRPC requests and receiving responses asynchronously. This can be used to
 * implement either plain gRPC or streaming gRPC calls.
 */
class AsyncClient {
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
  virtual AsyncRequest* send(const Protobuf::MethodDescriptor& service_method,
                             const Protobuf::Message& request, AsyncRequestCallbacks& callbacks,
                             Tracing::Span& parent_span,
                             const Optional<std::chrono::milliseconds>& timeout) PURE;

  /**
   * Start a gRPC stream asynchronously.
   * TODO(mattklein123): Determine if tracing should be added to streaming requests.
   * @param service_method protobuf descriptor of gRPC service method.
   * @param callbacks the callbacks to be notified of stream status.
   * @return a stream handle or nullptr if no stream could be started. NOTE: In this case
   *         onRemoteClose() has already been called inline. The client owns the stream and
   *         the handle can be used to send more messages or finish the stream. It is expected that
   *         closeStream() is invoked by the caller to notify the client that the stream resources
   *         may be reclaimed.
   */
  virtual AsyncStream* start(const Protobuf::MethodDescriptor& service_method,
                             AsyncStreamCallbacks& callbacks) PURE;
};

typedef std::unique_ptr<AsyncClient> AsyncClientPtr;

} // namespace Grpc
} // namespace Envoy
