#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"

#include "source/common/common/linked_object.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/http/async_client_impl.h"
#include "source/common/router/header_parser.h"

namespace Envoy {
namespace Grpc {

class AsyncRequestImpl;

class AsyncStreamImpl;
using AsyncStreamImplPtr = std::unique_ptr<AsyncStreamImpl>;

class AsyncClientImpl final : public RawAsyncClient {
public:
  AsyncClientImpl(Upstream::ClusterManager& cm, const envoy::config::core::v3::GrpcService& config,
                  TimeSource& time_source);
  ~AsyncClientImpl() override;

  // Grpc::AsyncClient
  AsyncRequest* sendRaw(absl::string_view service_full_name, absl::string_view method_name,
                        Buffer::InstancePtr&& request, RawAsyncRequestCallbacks& callbacks,
                        Tracing::Span& parent_span,
                        const Http::AsyncClient::RequestOptions& options) override;
  RawAsyncStream* startRaw(absl::string_view service_full_name, absl::string_view method_name,
                           RawAsyncStreamCallbacks& callbacks,
                           const Http::AsyncClient::StreamOptions& options) override;
  absl::string_view destination() override { return remote_cluster_name_; }

private:
  Upstream::ClusterManager& cm_;
  const std::string remote_cluster_name_;
  // The host header value in the http transport.
  const std::string host_name_;
  std::list<AsyncStreamImplPtr> active_streams_;
  TimeSource& time_source_;
  Router::HeaderParserPtr metadata_parser_;

  friend class AsyncRequestImpl;
  friend class AsyncStreamImpl;
};

class AsyncStreamImpl : public RawAsyncStream,
                        Http::AsyncClient::StreamCallbacks,
                        public Event::DeferredDeletable,
                        public LinkedObject<AsyncStreamImpl> {
public:
  AsyncStreamImpl(AsyncClientImpl& parent, absl::string_view service_full_name,
                  absl::string_view method_name, RawAsyncStreamCallbacks& callbacks,
                  const Http::AsyncClient::StreamOptions& options);

  virtual void initialize(bool buffer_body_for_retry);

  void sendMessage(const Protobuf::Message& request, bool end_stream);

  // Http::AsyncClient::StreamCallbacks
  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void onComplete() override;
  void onReset() override;

  // Grpc::AsyncStream
  void sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) override;
  void closeStream() override;
  void resetStream() override;
  bool isAboveWriteBufferHighWatermark() const override {
    return stream_ && stream_->isAboveWriteBufferHighWatermark();
  }

  bool hasResetStream() const { return http_reset_; }

private:
  void streamError(Status::GrpcStatus grpc_status, const std::string& message);
  void streamError(Status::GrpcStatus grpc_status) { streamError(grpc_status, EMPTY_STRING); }

  void cleanup();
  void trailerResponse(absl::optional<Status::GrpcStatus> grpc_status,
                       const std::string& grpc_message);

  Event::Dispatcher* dispatcher_{};
  Http::RequestMessagePtr headers_message_;
  AsyncClientImpl& parent_;
  std::string service_full_name_;
  std::string method_name_;
  RawAsyncStreamCallbacks& callbacks_;
  Http::AsyncClient::StreamOptions options_;
  bool http_reset_{};
  Http::AsyncClient::Stream* stream_{};
  Decoder decoder_;
  // This is a member to avoid reallocation on every onData().
  std::vector<Frame> decoded_frames_;

  friend class AsyncClientImpl;
};

class AsyncRequestImpl : public AsyncRequest, public AsyncStreamImpl, RawAsyncStreamCallbacks {
public:
  AsyncRequestImpl(AsyncClientImpl& parent, absl::string_view service_full_name,
                   absl::string_view method_name, Buffer::InstancePtr&& request,
                   RawAsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                   const Http::AsyncClient::RequestOptions& options);

  void initialize(bool buffer_body_for_retry) override;

  // Grpc::AsyncRequest
  void cancel() override;

private:
  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override;
  bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  Buffer::InstancePtr request_;
  RawAsyncRequestCallbacks& callbacks_;
  Tracing::SpanPtr current_span_;
  Buffer::InstancePtr response_;
};

} // namespace Grpc
} // namespace Envoy
