#pragma once

#include <chrono>

#include "envoy/grpc/async_client.h"

namespace Envoy {
namespace Grpc {

/**
 * Convenience wrapper for an AsyncStream* providing typed protobuf support.
 */
class UntypedAsyncStream {
public:
  UntypedAsyncStream() {}
  UntypedAsyncStream(AsyncStream* stream, bool is_grpc_header_required)
      : stream_(stream), is_grpc_header_required_(is_grpc_header_required) {}
  void sendMessage(Buffer::InstancePtr&& request, bool end_stream) {
    stream_->sendMessage(std::move(request), end_stream);
  }
  void closeStream() { stream_->closeStream(); }
  void resetStream() { stream_->resetStream(); }
  void sendMessageUntyped(const Protobuf::Message& request, bool end_stream);
  UntypedAsyncStream* operator->() { return this; }
  bool operator==(AsyncStream* stream) const { return stream_ == stream; }
  bool operator!=(AsyncStream* stream) const { return stream_ != stream; }

private:
  template <typename Request> friend class TypedAsyncStream;
  AsyncStream* stream_{};
  bool is_grpc_header_required_{};
};

template <typename Request> class TypedAsyncStream : public UntypedAsyncStream {
public:
  TypedAsyncStream() {}
  TypedAsyncStream(AsyncStream* stream, bool is_grpc_header_required)
      : UntypedAsyncStream(stream, is_grpc_header_required) {}
  TypedAsyncStream(UntypedAsyncStream&& other)
      : TypedAsyncStream(std::move(other.stream_), other.is_grpc_header_required_) {}
  void sendMessageTyped(const Request& request, bool end_stream) {
    sendMessageUntyped(request, end_stream);
  }
  TypedAsyncStream* operator->() { return this; }
  TypedAsyncStream<Request> operator=(AsyncStream* stream) {
    stream_ = stream;
    return *this;
  }
};

/**
 * Convenience subclasses for AsyncRequestCallbacks.
 */
class UntypedAsyncRequestCallbacks : public AsyncRequestCallbacks {
public:
  virtual ~UntypedAsyncRequestCallbacks() {}
  virtual void onSuccessUntyped(ProtobufTypes::MessagePtr&& response, Tracing::Span& span) PURE;

protected:
  void onSuccess(Buffer::InstancePtr&& response, Tracing::Span& span) override;
  virtual ProtobufTypes::MessagePtr createEmptyResponse() { NOT_REACHED_GCOVR_EXCL_LINE; }
};

template <typename Response>
class TypedAsyncRequestCallbacks : public UntypedAsyncRequestCallbacks {
public:
  virtual ~TypedAsyncRequestCallbacks() {}
  virtual void onSuccessTyped(std::unique_ptr<Response>&& response, Tracing::Span& span) PURE;

private:
  ProtobufTypes::MessagePtr createEmptyResponse() override { return std::make_unique<Response>(); }
  void onSuccessUntyped(ProtobufTypes::MessagePtr&& response, Tracing::Span& span) override {
    onSuccessTyped(std::unique_ptr<Response>(dynamic_cast<Response*>(response.release())), span);
  }
};

/**
 * Convenience subclasses for AsyncStreamCallbacks.
 */
class UntypedAsyncStreamCallbacks : public AsyncStreamCallbacks {
public:
  virtual ~UntypedAsyncStreamCallbacks() {}
  virtual void onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& response) PURE;

protected:
  virtual ProtobufTypes::MessagePtr createEmptyResponse() { NOT_REACHED_GCOVR_EXCL_LINE; }
  bool onReceiveMessage(Buffer::InstancePtr&& response) override;
};

template <typename Response> class TypedAsyncStreamCallbacks : public UntypedAsyncStreamCallbacks {
public:
  virtual ~TypedAsyncStreamCallbacks() {}
  virtual void onReceiveMessageTyped(std::unique_ptr<Response>&& message) PURE;

private:
  ProtobufTypes::MessagePtr createEmptyResponse() override { return std::make_unique<Response>(); }
  void onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& response) override {
    onReceiveMessageTyped(std::unique_ptr<Response>(dynamic_cast<Response*>(response.release())));
  }
};

class UntypedAsyncClient {
public:
  UntypedAsyncClient() {}
  UntypedAsyncClient(AsyncClientPtr&& client) : client_(std::move(client)) {}
  AsyncRequest* sendUntyped(const Protobuf::MethodDescriptor& service_method,
                            const Protobuf::Message& request,
                            UntypedAsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                            const absl::optional<std::chrono::milliseconds>& timeout);
  UntypedAsyncStream startUntyped(const Protobuf::MethodDescriptor& service_method,
                                  UntypedAsyncStreamCallbacks& callbacks);

  UntypedAsyncClient* operator->() { return this; }
  void operator=(AsyncClientPtr&& client) { client_ = std::move(client); }
  void reset() { client_.reset(); }

private:
  template <typename Request, typename Response> friend class TypedAsyncClient;
  AsyncClientPtr client_{};
};

template <typename Request, typename Response> class TypedAsyncClient : public UntypedAsyncClient {
public:
  TypedAsyncClient() {}
  TypedAsyncClient(AsyncClientPtr&& client) : UntypedAsyncClient(std::move(client)) {}
  TypedAsyncClient(UntypedAsyncClient&& other) : UntypedAsyncClient(std::move(other.client_)) {}

  AsyncRequest* sendTyped(const Protobuf::MethodDescriptor& service_method,
                          const Protobuf::Message& request,
                          TypedAsyncRequestCallbacks<Response>& callbacks,
                          Tracing::Span& parent_span,
                          const absl::optional<std::chrono::milliseconds>& timeout) {
    return sendUntyped(service_method, request, callbacks, parent_span, timeout);
  }
  TypedAsyncStream<Request> startTyped(const Protobuf::MethodDescriptor& service_method,
                                       TypedAsyncStreamCallbacks<Response>& callbacks) {
    return TypedAsyncStream<Request>(startUntyped(service_method, callbacks));
  }

  TypedAsyncClient* operator->() { return this; }
  void operator=(AsyncClientPtr&& client) { client_ = std::move(client); }
};

} // namespace Grpc
} // namespace Envoy
