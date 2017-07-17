#pragma once

#include "envoy/grpc/async_client.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/linked_object.h"
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
  AsyncClientImpl(Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                  const std::string& remote_cluster_name)
      : cm_(cm), dispatcher_(dispatcher), remote_cluster_name_(remote_cluster_name) {}

  ~AsyncClientImpl() override {
    while (!active_streams_.empty()) {
      active_streams_.front()->resetStream();
    }
  }

  // Grpc::AsyncClient
  AsyncRequest* send(const Protobuf::MethodDescriptor& service_method, const RequestType& request,
                     AsyncRequestCallbacks<ResponseType>& callbacks,
                     const Optional<std::chrono::milliseconds>& timeout) override {
    std::unique_ptr<AsyncStreamImpl<RequestType, ResponseType>> grpc_stream{
        new AsyncRequestImpl<RequestType, ResponseType>(*this, service_method, request, callbacks,
                                                        timeout)};

    grpc_stream->initialize();
    if (grpc_stream->hasResetStream()) {
      return nullptr;
    }

    grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
    return dynamic_cast<AsyncRequestImpl<RequestType, ResponseType>*>(
        active_streams_.front().get());
  }

  AsyncStream<RequestType>* start(const Protobuf::MethodDescriptor& service_method,
                                  AsyncStreamCallbacks<ResponseType>& callbacks,
                                  const Optional<std::chrono::milliseconds>& timeout) override {
    std::unique_ptr<AsyncStreamImpl<RequestType, ResponseType>> grpc_stream{
        new AsyncStreamImpl<RequestType, ResponseType>(*this, service_method, callbacks, timeout)};

    grpc_stream->initialize();
    if (grpc_stream->hasResetStream()) {
      return nullptr;
    }

    grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
    return active_streams_.front().get();
  }

private:
  Upstream::ClusterManager& cm_;
  Event::Dispatcher& dispatcher_;
  const std::string remote_cluster_name_;
  std::list<std::unique_ptr<AsyncStreamImpl<RequestType, ResponseType>>> active_streams_;

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

  virtual void initialize() {
    stream_ = parent_.cm_.httpAsyncClientForCluster(parent_.remote_cluster_name_)
                  .start(*this, Optional<std::chrono::milliseconds>(timeout_));

    if (stream_ == nullptr) {
      callbacks_.onRemoteClose(Status::GrpcStatus::Unavailable);
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
      streamError(grpc_status.value());
      return;
    }
    callbacks_.onReceiveTrailingMetadata(std::move(trailers));
    callbacks_.onRemoteClose(Status::GrpcStatus::Ok);
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
  void streamError(Status::GrpcStatus grpc_status) {
    callbacks_.onRemoteClose(grpc_status);
    resetStream();
  }

  void cleanup() {
    if (!http_reset_) {
      http_reset_ = true;
      stream_->reset();
    }

    // This will destroy us, but only do so if we are actually in a list. This does not happen in
    // the immediate failure case.
    if (LinkedObject<AsyncStreamImpl<RequestType, ResponseType>>::inserted()) {
      parent_.dispatcher_.deferredDelete(
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

template <class RequestType, class ResponseType>
class AsyncRequestImpl : public AsyncRequest,
                         public AsyncStreamImpl<RequestType, ResponseType>,
                         AsyncStreamCallbacks<ResponseType> {
public:
  AsyncRequestImpl(AsyncClientImpl<RequestType, ResponseType>& parent,
                   const Protobuf::MethodDescriptor& service_method, const RequestType& request,
                   AsyncRequestCallbacks<ResponseType>& callbacks,
                   const Optional<std::chrono::milliseconds>& timeout)
      : AsyncStreamImpl<RequestType, ResponseType>(parent, service_method, *this, timeout),
        request_(request), callbacks_(callbacks) {}

  void initialize() override {
    AsyncStreamImpl<RequestType, ResponseType>::initialize();
    if (this->hasResetStream()) {
      return;
    }
    this->sendMessage(request_, true);
  }

  // Grpc::AsyncRequest
  void cancel() override { this->resetStream(); }

private:
  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override {
    callbacks_.onCreateInitialMetadata(metadata);
  }

  void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override {}

  void onReceiveMessage(std::unique_ptr<ResponseType>&& message) override {
    response_ = std::move(message);
  }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override {}

  void onRemoteClose(Grpc::Status::GrpcStatus status) override {
    if (status != Grpc::Status::GrpcStatus::Ok) {
      callbacks_.onFailure(status);
      return;
    }
    if (response_ == nullptr) {
      callbacks_.onFailure(Status::Internal);
      return;
    }

    callbacks_.onSuccess(std::move(response_));
  }

  const RequestType& request_;
  AsyncRequestCallbacks<ResponseType>& callbacks_;
  std::unique_ptr<ResponseType> response_;
};

} // namespace Grpc
} // namespace Envoy
