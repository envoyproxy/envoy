#pragma once

#include <chrono>
#include <memory>

#include "envoy/grpc/async_client.h"

#include "source/common/common/empty_string.h"

namespace Envoy {
namespace Grpc {
namespace Internal {

/**
 * Forward declarations for helper functions.
 */
void sendMessageUntyped(RawAsyncStream* stream, const Protobuf::Message& request, bool end_stream);
ProtobufTypes::MessagePtr parseMessageUntyped(ProtobufTypes::MessagePtr&& message,
                                              Buffer::InstancePtr&& response);
RawAsyncStream* startUntyped(RawAsyncClient* client,
                             const Protobuf::MethodDescriptor& service_method,
                             RawAsyncStreamCallbacks& callbacks,
                             const Http::AsyncClient::StreamOptions& options);
AsyncRequest* sendUntyped(RawAsyncClient* client, const Protobuf::MethodDescriptor& service_method,
                          const Protobuf::Message& request, RawAsyncRequestCallbacks& callbacks,
                          Tracing::Span& parent_span,
                          const Http::AsyncClient::RequestOptions& options);

} // namespace Internal

/**
 * Convenience wrapper for an AsyncStream* providing typed protobuf support.
 */
template <typename Request> class AsyncStream /* : public RawAsyncStream */ {
public:
  AsyncStream() = default;
  AsyncStream(RawAsyncStream* stream) : stream_(stream) {}
  void sendMessage(const Protobuf::Message& request, bool end_stream) {
    Internal::sendMessageUntyped(stream_, std::move(request), end_stream);
  }
  void closeStream() { stream_->closeStream(); }
  void resetStream() { stream_->resetStream(); }
  bool isAboveWriteBufferHighWatermark() const {
    return stream_->isAboveWriteBufferHighWatermark();
  }
  AsyncStream* operator->() { return this; }
  AsyncStream<Request> operator=(RawAsyncStream* stream) {
    stream_ = stream;
    return *this;
  }
  bool operator==(RawAsyncStream* stream) const { return stream_ == stream; }
  bool operator!=(RawAsyncStream* stream) const { return stream_ != stream; }
  const StreamInfo::StreamInfo& streamInfo() const { return stream_->streamInfo(); }

private:
  RawAsyncStream* stream_{};
};

template <typename Response> using ResponsePtr = std::unique_ptr<Response>;

/**
 * Convenience subclasses for AsyncRequestCallbacks.
 */
template <typename Response> class AsyncRequestCallbacks : public RawAsyncRequestCallbacks {
public:
  ~AsyncRequestCallbacks() override = default;
  virtual void onSuccess(ResponsePtr<Response>&& response, Tracing::Span& span) PURE;

private:
  void onSuccessRaw(Buffer::InstancePtr&& response, Tracing::Span& span) override {
    auto message = ResponsePtr<Response>(dynamic_cast<Response*>(
        Internal::parseMessageUntyped(std::make_unique<Response>(), std::move(response))
            .release()));
    if (!message) {
      onFailure(Status::WellKnownGrpcStatus::Internal, "", span);
      return;
    }
    onSuccess(std::move(message), span);
  }
};

/**
 * Convenience subclasses for AsyncStreamCallbacks.
 */
template <typename Response> class AsyncStreamCallbacks : public RawAsyncStreamCallbacks {
public:
  ~AsyncStreamCallbacks() override = default;
  virtual void onReceiveMessage(ResponsePtr<Response>&& message) PURE;

private:
  bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
    auto message = ResponsePtr<Response>(dynamic_cast<Response*>(
        Internal::parseMessageUntyped(std::make_unique<Response>(), std::move(response))
            .release()));
    if (!message) {
      return false;
    }
    onReceiveMessage(std::move(message));
    return true;
  }
};

template <typename Request, typename Response> class AsyncClient /* : public RawAsyncClient )*/ {
public:
  AsyncClient() = default;
  AsyncClient(RawAsyncClientPtr&& client) : client_(std::move(client)) {}
  AsyncClient(RawAsyncClientSharedPtr client) : client_(client) {}
  virtual ~AsyncClient() = default;

  virtual AsyncRequest* send(const Protobuf::MethodDescriptor& service_method,
                             const Protobuf::Message& request,
                             AsyncRequestCallbacks<Response>& callbacks, Tracing::Span& parent_span,
                             const Http::AsyncClient::RequestOptions& options) {
    return Internal::sendUntyped(client_.get(), service_method, request, callbacks, parent_span,
                                 options);
  }

  virtual AsyncStream<Request> start(const Protobuf::MethodDescriptor& service_method,
                                     AsyncStreamCallbacks<Response>& callbacks,
                                     const Http::AsyncClient::StreamOptions& options) {
    return AsyncStream<Request>(
        Internal::startUntyped(client_.get(), service_method, callbacks, options));
  }

  absl::string_view destination() { return client_->destination(); }

  AsyncClient* operator->() { return this; }
  void operator=(RawAsyncClientPtr&& client) { client_ = std::move(client); }
  void reset() { client_.reset(); }

private:
  RawAsyncClientSharedPtr client_{};
};

} // namespace Grpc
} // namespace Envoy
