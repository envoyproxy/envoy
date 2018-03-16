#pragma once

#include "envoy/grpc/async_client.h"

#include "common/common/linked_object.h"
#include "common/grpc/codec.h"
#include "common/http/async_client_impl.h"

namespace Envoy {
namespace Grpc {

class AsyncRequestImpl;
class AsyncStreamImpl;

class AsyncClientImpl final : public AsyncClient {
public:
  AsyncClientImpl(Upstream::ClusterManager& cm, const envoy::api::v2::core::GrpcService& config);
  ~AsyncClientImpl() override;

  // Grpc::AsyncClient
  AsyncRequest* send(const Protobuf::MethodDescriptor& service_method,
                     const Protobuf::Message& request, AsyncRequestCallbacks& callbacks,
                     Tracing::Span& parent_span,
                     const absl::optional<std::chrono::milliseconds>& timeout) override;
  AsyncStream* start(const Protobuf::MethodDescriptor& service_method,
                     AsyncStreamCallbacks& callbacks) override;

private:
  Upstream::ClusterManager& cm_;
  const std::string remote_cluster_name_;
  const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue> initial_metadata_;
  std::list<std::unique_ptr<AsyncStreamImpl>> active_streams_;

  friend class AsyncRequestImpl;
  friend class AsyncStreamImpl;
};

class AsyncStreamImpl : public AsyncStream,
                        Http::AsyncClient::StreamCallbacks,
                        public Event::DeferredDeletable,
                        LinkedObject<AsyncStreamImpl> {
public:
  AsyncStreamImpl(AsyncClientImpl& parent, const Protobuf::MethodDescriptor& service_method,
                  AsyncStreamCallbacks& callbacks,
                  const absl::optional<std::chrono::milliseconds>& timeout);

  virtual void initialize(bool buffer_body_for_retry);

  // Http::AsyncClient::StreamCallbacks
  void onHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::HeaderMapPtr&& trailers) override;
  void onReset() override;

  // Grpc::AsyncStream
  void sendMessage(const Protobuf::Message& request, bool end_stream) override;
  void closeStream() override;
  void resetStream() override;

  bool hasResetStream() const { return http_reset_; }

private:
  void streamError(Status::GrpcStatus grpc_status, const std::string& message);
  void streamError(Status::GrpcStatus grpc_status) { streamError(grpc_status, EMPTY_STRING); }

  void cleanup();
  void trailerResponse(absl::optional<Status::GrpcStatus> grpc_status,
                       const std::string& grpc_message);

  Event::Dispatcher* dispatcher_{};
  Http::MessagePtr headers_message_;
  AsyncClientImpl& parent_;
  const Protobuf::MethodDescriptor& service_method_;
  AsyncStreamCallbacks& callbacks_;
  const absl::optional<std::chrono::milliseconds>& timeout_;
  bool http_reset_{};
  Http::AsyncClient::Stream* stream_{};
  Decoder decoder_;
  // This is a member to avoid reallocation on every onData().
  std::vector<Frame> decoded_frames_;

  friend class AsyncClientImpl;
};

class AsyncRequestImpl : public AsyncRequest, public AsyncStreamImpl, AsyncStreamCallbacks {
public:
  AsyncRequestImpl(AsyncClientImpl& parent, const Protobuf::MethodDescriptor& service_method,
                   const Protobuf::Message& request, AsyncRequestCallbacks& callbacks,
                   Tracing::Span& parent_span,
                   const absl::optional<std::chrono::milliseconds>& timeout);

  void initialize(bool buffer_body_for_retry) override;

  // Grpc::AsyncRequest
  void cancel() override;

private:
  // Grpc::AsyncStreamCallbacks
  ProtobufTypes::MessagePtr createEmptyResponse() override;
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override;
  void onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  const Protobuf::Message& request_;
  AsyncRequestCallbacks& callbacks_;
  Tracing::SpanPtr current_span_;
  ProtobufTypes::MessagePtr response_;
};

} // namespace Grpc
} // namespace Envoy
