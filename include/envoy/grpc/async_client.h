#pragma once

#include <chrono>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"
#include "envoy/tracing/http_tracer.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Grpc {

/**
 * An in-flight gRPC unary RPC.
 */
class AsyncRequest {
public:
  virtual ~AsyncRequest() = default;

  /**
   * Signals that the request should be cancelled. No further callbacks will be invoked.
   */
  virtual void cancel() PURE;
};

/**
 * An in-flight gRPC stream.
 */
class RawAsyncStream {
public:
  virtual ~RawAsyncStream() = default;

  /**
   * Send request message to the stream.
   * @param request serialized message.
   * @param end_stream close the stream locally. No further methods may be invoked on the stream
   *                   object, but callbacks may still be received until the stream is closed
   *                   remotely.
   */
  virtual void sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) PURE;

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

class RawAsyncRequestCallbacks {
public:
  virtual ~RawAsyncRequestCallbacks() = default;

  /**
   * Called when populating the headers to send with initial metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) PURE;

  /**
   * Called when the async gRPC request succeeds. No further callbacks will be invoked.
   * @param response the gRPC response bytes.
   * @param span a tracing span to fill with extra tags.
   */
  virtual void onSuccessRaw(Buffer::InstancePtr&& response, Tracing::Span& span) PURE;

  /**
   * Called when the async gRPC request fails. No further callbacks will be invoked.
   * @param status the gRPC status.
   * @param message the gRPC status message or empty string if not present.
   * @param span a tracing span to fill with extra tags.
   */
  virtual void onFailure(Status::GrpcStatus status, const std::string& message,
                         Tracing::Span& span) PURE;
};

/**
 * Notifies caller of async gRPC stream status.
 * Note the gRPC stream is full-duplex, even if the local to remote stream has been ended by
 * AsyncStream.close(), AsyncStreamCallbacks can continue to receive events until the remote
 * to local stream is closed (onRemoteClose), and vice versa. Once the stream is closed remotely, no
 * further callbacks will be invoked.
 */
class RawAsyncStreamCallbacks {
public:
  virtual ~RawAsyncStreamCallbacks() = default;

  /**
   * Called when populating the headers to send with initial metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) PURE;

  /**
   * Called when initial metadata is received. This will be called with empty metadata on a
   * trailers-only response, followed by onReceiveTrailingMetadata() with the trailing metadata.
   * @param metadata initial metadata reference.
   */
  virtual void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) PURE;

  /**
   * Called when an async gRPC message is received.
   * @param response the gRPC message.
   * @return bool which is true if the message well formed and false otherwise which will cause
              the stream to shutdown with an INTERNAL error.
   */
  virtual bool onReceiveMessageRaw(Buffer::InstancePtr&& response) PURE;

  /**
   * Called when trailing metadata is received. This will also be called on non-Ok grpc-status
   * stream termination.
   * @param metadata trailing metadata reference.
   */
  virtual void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) PURE;

  /**
   * Called when the remote closes or an error occurs on the gRPC stream. The stream is
   * considered remotely closed after this invocation and no further callbacks will be
   * invoked. In addition, no further stream operations are permitted.
   * @param status the gRPC status.
   * @param message the gRPC status message or empty string if not present.
   */
  virtual void onRemoteClose(Status::GrpcStatus status, const std::string& message) PURE;
};

/**
 * Supports sending gRPC requests and receiving responses asynchronously. This can be used to
 * implement either plain gRPC or streaming gRPC calls.
 */
class RawAsyncClient {
public:
  virtual ~RawAsyncClient() = default;

  /**
   * Start a gRPC unary RPC asynchronously.
   * @param service_full_name full name of the service (i.e. service_method.service()->full_name()).
   * @param method_name name of the method (i.e. service_method.name()).
   * @param request serialized message.
   * @param callbacks the callbacks to be notified of RPC status.
   * @param parent_span the current parent tracing context.
   * @param options the data struct to control the request sending.
   * @return a request handle or nullptr if no request could be started. NOTE: In this case
   *         onFailure() has already been called inline. The client owns the request and the
   *         handle should just be used to cancel.
   */
  virtual AsyncRequest* sendRaw(absl::string_view service_full_name, absl::string_view method_name,
                                Buffer::InstancePtr&& request, RawAsyncRequestCallbacks& callbacks,
                                Tracing::Span& parent_span,
                                const Http::AsyncClient::RequestOptions& options) PURE;

  /**
   * Start a gRPC stream asynchronously.
   * TODO(mattklein123): Determine if tracing should be added to streaming requests.
   * @param service_full_name full name of the service (i.e. service_method.service()->full_name()).
   * @param method_name name of the method (i.e. service_method.name()).
   * @param callbacks the callbacks to be notified of stream status.
   * @param options the data struct to control the stream.
   * @return a stream handle or nullptr if no stream could be started. NOTE: In this case
   *         onRemoteClose() has already been called inline. The client owns the stream and
   *         the handle can be used to send more messages or finish the stream. It is expected that
   *         closeStream() is invoked by the caller to notify the client that the stream resources
   *         may be reclaimed.
   */
  virtual RawAsyncStream* startRaw(absl::string_view service_full_name,
                                   absl::string_view method_name,
                                   RawAsyncStreamCallbacks& callbacks,
                                   const Http::AsyncClient::StreamOptions& options) PURE;
};

using RawAsyncClientPtr = std::unique_ptr<RawAsyncClient>;

} // namespace Grpc
} // namespace Envoy
