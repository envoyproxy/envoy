#pragma once

#include "envoy/grpc/async_client.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/linked_object.h"
#include "common/common/utility.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/http/async_client_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Grpc {

template <class RequestType, class ResponseType> class AsyncStreamImpl;
template <class RequestType, class ResponseType> class AsyncRequestImpl;

template <class RequestType, class ResponseType>
class AsyncClientImpl final : public AsyncClient<RequestType, ResponseType> {
public:
  AsyncClientImpl(Upstream::ClusterManager& cm, const std::string& remote_cluster_name)
      : cm_(cm), remote_cluster_name_(remote_cluster_name) {}

  ~AsyncClientImpl() override {
    while (!active_streams_.empty()) {
      active_streams_.front()->resetStream();
    }
  }

  // Grpc::AsyncClient
  AsyncRequest* send(const Protobuf::MethodDescriptor& service_method, const RequestType& request,
                     AsyncRequestCallbacks<ResponseType>& callbacks, Tracing::Span& parent_span,
                     const Optional<std::chrono::milliseconds>& timeout) override {
    auto* const async_request = new AsyncRequestImpl<RequestType, ResponseType>(
        *this, service_method, request, callbacks, parent_span, timeout);
    std::unique_ptr<AsyncStreamImpl<RequestType, ResponseType>> grpc_stream{async_request};

    grpc_stream->initialize(true);
    if (grpc_stream->hasResetStream()) {
      return nullptr;
    }

    grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
    return async_request;
  }

  AsyncStream<RequestType>* start(const Protobuf::MethodDescriptor& service_method,
                                  AsyncStreamCallbacks<ResponseType>& callbacks) override {
    const Optional<std::chrono::milliseconds> no_timeout;
    std::unique_ptr<AsyncStreamImpl<RequestType, ResponseType>> grpc_stream{
        new AsyncStreamImpl<RequestType, ResponseType>(*this, service_method, callbacks,
                                                       no_timeout)};

    grpc_stream->initialize(false);
    if (grpc_stream->hasResetStream()) {
      return nullptr;
    }

    grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
    return active_streams_.front().get();
  }

private:
  Upstream::ClusterManager& cm_;
  const std::string remote_cluster_name_;
  std::list<std::unique_ptr<AsyncStreamImpl<RequestType, ResponseType>>> active_streams_;

  friend class AsyncRequestImpl<RequestType, ResponseType>;
  friend class AsyncStreamImpl<RequestType, ResponseType>;
};

template <class RequestType, class ResponseType>
class AsyncStreamImpl : public AsyncStream<RequestType>,
                        Http::AsyncClient::StreamCallbacks,
                        public Event::DeferredDeletable,
                        LinkedObject<AsyncStreamImpl<RequestType, ResponseType>> {
public:
  AsyncStreamImpl(AsyncClientImpl<RequestType, ResponseType>& parent,
                  const Protobuf::MethodDescriptor& service_method,
                  AsyncStreamCallbacks<ResponseType>& callbacks,
                  const Optional<std::chrono::milliseconds>& timeout)
      : parent_(parent), service_method_(service_method), callbacks_(callbacks), timeout_(timeout) {
  }

  virtual void initialize(bool buffer_body_for_retry) {
    auto& http_async_client = parent_.cm_.httpAsyncClientForCluster(parent_.remote_cluster_name_);
    dispatcher_ = &http_async_client.dispatcher();
    stream_ = http_async_client.start(*this, Optional<std::chrono::milliseconds>(timeout_),
                                      buffer_body_for_retry);

    if (stream_ == nullptr) {
      callbacks_.onRemoteClose(Status::GrpcStatus::Unavailable, EMPTY_STRING);
      http_reset_ = true;
      return;
    }

    headers_message_ =
        Common::prepareHeaders(parent_.remote_cluster_name_, service_method_.service()->full_name(),
                               service_method_.name());
    callbacks_.onCreateInitialMetadata(headers_message_->headers());
    stream_->sendHeaders(headers_message_->headers(), false);
  }

  // Http::AsyncClient::StreamCallbacks
  void onHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override {
    ASSERT(!remote_closed_);
    const auto http_response_status = Http::Utility::getResponseStatus(*headers);
    if (http_response_status != enumToInt(Http::Code::OK)) {
      // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md requires that
      // grpc-status be used if available.
      if (end_stream && Common::getGrpcStatus(*headers).valid()) {
        onTrailers(std::move(headers));
        return;
      }
      streamError(Common::httpToGrpcStatus(http_response_status));
      return;
    }
    if (end_stream) {
      onTrailers(std::move(headers));
      return;
    }
    callbacks_.onReceiveInitialMetadata(std::move(headers));
  }

  void onData(Buffer::Instance& data, bool end_stream) override {
    ASSERT(!remote_closed_);
    if (end_stream) {
      streamError(Status::GrpcStatus::Internal);
      return;
    }

    decoded_frames_.clear();
    if (!decoder_.decode(data, decoded_frames_)) {
      streamError(Status::GrpcStatus::Internal);
      return;
    }

    for (auto& frame : decoded_frames_) {
      std::unique_ptr<ResponseType> response(new ResponseType());
      // TODO(htuch): Need to add support for compressed responses as well here.
      if (frame.length_ > 0) {
        Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));

        if (frame.flags_ != GRPC_FH_DEFAULT || !response->ParseFromZeroCopyStream(&stream)) {
          streamError(Status::GrpcStatus::Internal);
          return;
        }
      }
      callbacks_.onReceiveMessage(std::move(response));
    }
  }

  void onTrailers(Http::HeaderMapPtr&& trailers) override {
    ASSERT(!remote_closed_);

    const Optional<Status::GrpcStatus> grpc_status = Common::getGrpcStatus(*trailers);
    if (!grpc_status.valid()) {
      streamError(Status::GrpcStatus::Internal);
      return;
    }
    if (grpc_status.value() != Status::GrpcStatus::Ok) {
      const std::string grpc_message = Common::getGrpcMessage(*trailers);
      streamError(grpc_status.value(), grpc_message);
      return;
    }
    callbacks_.onReceiveTrailingMetadata(std::move(trailers));
    callbacks_.onRemoteClose(Status::GrpcStatus::Ok, EMPTY_STRING);
    closeRemote();
  }

  void onReset() override {
    if (http_reset_) {
      return;
    }

    http_reset_ = true;
    streamError(Status::GrpcStatus::Internal);
  }

  // Grpc::AsyncStream
  void sendMessage(const RequestType& request, bool end_stream) override {
    stream_->sendData(*Common::serializeBody(request), end_stream);
    if (end_stream) {
      closeLocal();
    }
  }

  void closeStream() override {
    Buffer::OwnedImpl empty_buffer;
    stream_->sendData(empty_buffer, true);
    closeLocal();
  }

  void resetStream() override {
    // Both closeLocal() and closeRemote() might self-destruct the object. We don't use these below
    // to avoid sequencing issues.
    local_closed_ |= true;
    remote_closed_ |= true;
    cleanup();
  }

  bool hasResetStream() const { return http_reset_; }

private:
  void streamError(Status::GrpcStatus grpc_status, const std::string& message) {
    callbacks_.onRemoteClose(grpc_status, message);
    resetStream();
  }

  void streamError(Status::GrpcStatus grpc_status) { streamError(grpc_status, EMPTY_STRING); }

  void cleanup() {
    if (!http_reset_) {
      http_reset_ = true;
      stream_->reset();
    }

    // This will destroy us, but only do so if we are actually in a list. This does not happen in
    // the immediate failure case.
    if (LinkedObject<AsyncStreamImpl<RequestType, ResponseType>>::inserted()) {
      dispatcher_->deferredDelete(
          LinkedObject<AsyncStreamImpl<RequestType, ResponseType>>::removeFromList(
              parent_.active_streams_));
    }
  }

  void closeLocal() {
    local_closed_ |= true;
    if (complete()) {
      cleanup();
    }
  }

  void closeRemote() {
    remote_closed_ |= true;
    if (complete()) {
      cleanup();
    }
  }

  bool complete() const { return local_closed_ && remote_closed_; }

  Event::Dispatcher* dispatcher_{};
  Http::MessagePtr headers_message_;
  AsyncClientImpl<RequestType, ResponseType>& parent_;
  const Protobuf::MethodDescriptor& service_method_;
  AsyncStreamCallbacks<ResponseType>& callbacks_;
  const Optional<std::chrono::milliseconds>& timeout_;
  bool local_closed_{};
  bool remote_closed_{};
  bool http_reset_{};
  Http::AsyncClient::Stream* stream_{};
  Decoder decoder_;
  // This is a member to avoid reallocation on every onData().
  std::vector<Frame> decoded_frames_;

  friend class AsyncClientImpl<RequestType, ResponseType>;
};

class AsyncClientTracingConfig : public Tracing::EgressConfigImpl {
public:
  struct {
    const std::string STATUS = "status";
    const std::string GRPC_STATUS = "grpc.status_code";
    const std::string CANCELED = "canceled";
    const std::string ERROR = "error";
    const std::string TRUE = "true";
    const std::string UPSTREAM_CLUSTER_NAME = "upstream_cluster_name";
  } TagStrings;
};

typedef ConstSingleton<AsyncClientTracingConfig> TracingConfig;

template <class RequestType, class ResponseType>
class AsyncRequestImpl : public AsyncRequest,
                         public AsyncStreamImpl<RequestType, ResponseType>,
                         AsyncStreamCallbacks<ResponseType> {
public:
  AsyncRequestImpl(AsyncClientImpl<RequestType, ResponseType>& parent,
                   const Protobuf::MethodDescriptor& service_method, const RequestType& request,
                   AsyncRequestCallbacks<ResponseType>& callbacks, Tracing::Span& parent_span,
                   const Optional<std::chrono::milliseconds>& timeout)
      : AsyncStreamImpl<RequestType, ResponseType>(parent, service_method, *this, timeout),
        request_(request), callbacks_(callbacks) {

    current_span_ = parent_span.spawnChild(TracingConfig::get(),
                                           "async " + parent.remote_cluster_name_ + " egress",
                                           ProdSystemTimeSource::instance_.currentTime());
    current_span_->setTag(TracingConfig::get().TagStrings.UPSTREAM_CLUSTER_NAME,
                          parent.remote_cluster_name_);
  }

  void initialize(bool buffer_body_for_retry) override {
    AsyncStreamImpl<RequestType, ResponseType>::initialize(buffer_body_for_retry);
    if (this->hasResetStream()) {
      return;
    }
    this->sendMessage(request_, true);
  }

  // Grpc::AsyncRequest
  void cancel() override {
    current_span_->setTag(TracingConfig::get().TagStrings.STATUS,
                          TracingConfig::get().TagStrings.CANCELED);
    current_span_->finishSpan();
    this->resetStream();
  }

private:
  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override {
    current_span_->injectContext(metadata);
    callbacks_.onCreateInitialMetadata(metadata);
  }

  void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override {}

  void onReceiveMessage(std::unique_ptr<ResponseType>&& message) override {
    response_ = std::move(message);
  }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override {}

  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
    current_span_->setTag(TracingConfig::get().TagStrings.GRPC_STATUS, std::to_string(status));

    if (status != Grpc::Status::GrpcStatus::Ok) {
      current_span_->setTag(TracingConfig::get().TagStrings.ERROR,
                            TracingConfig::get().TagStrings.TRUE);
      callbacks_.onFailure(status, message, *current_span_);
    } else if (response_ == nullptr) {
      current_span_->setTag(TracingConfig::get().TagStrings.ERROR,
                            TracingConfig::get().TagStrings.TRUE);
      callbacks_.onFailure(Status::Internal, EMPTY_STRING, *current_span_);
    } else {
      callbacks_.onSuccess(std::move(response_), *current_span_);
    }

    current_span_->finishSpan();
  }

  const RequestType& request_;
  AsyncRequestCallbacks<ResponseType>& callbacks_;
  Tracing::SpanPtr current_span_;
  std::unique_ptr<ResponseType> response_;
};

} // namespace Grpc
} // namespace Envoy
