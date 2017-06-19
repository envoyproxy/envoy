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

template <class RequestType, class ResponseType> class AsyncClientStreamImpl;

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
  AsyncClientStream<RequestType>*
  start(const google::protobuf::MethodDescriptor& service_method,
        AsyncClientCallbacks<ResponseType>& callbacks,
        const Optional<std::chrono::milliseconds>& timeout) override {
    std::unique_ptr<AsyncClientStreamImpl<RequestType, ResponseType>> grpc_stream{
        new AsyncClientStreamImpl<RequestType, ResponseType>(*this, callbacks)};
    Http::AsyncClient::Stream* http_stream =
        cm_.httpAsyncClientForCluster(remote_cluster_name_)
            .start(*grpc_stream, Optional<std::chrono::milliseconds>(timeout));

    if (http_stream == nullptr) {
      callbacks.onRemoteClose(Status::GrpcStatus::Unavailable);
      return nullptr;
    }

    grpc_stream->set_stream(http_stream);

    Http::MessagePtr message = Common::prepareHeaders(
        remote_cluster_name_, service_method.service()->full_name(), service_method.name());
    callbacks.onCreateInitialMetadata(message->headers());

    http_stream->sendHeaders(message->headers(), false);
    // If sendHeaders() caused a reset, onRemoteClose() has been called inline and we should bail.
    if (grpc_stream->http_reset_) {
      return nullptr;
    }

    grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
    return active_streams_.front().get();
  }

private:
  Upstream::ClusterManager& cm_;
  const std::string remote_cluster_name_;
  std::list<std::unique_ptr<AsyncClientStreamImpl<RequestType, ResponseType>>> active_streams_;

  friend class AsyncClientStreamImpl<RequestType, ResponseType>;
};

template <class RequestType, class ResponseType>
class AsyncClientStreamImpl : public AsyncClientStream<RequestType>,
                              Http::AsyncClient::StreamCallbacks,
                              LinkedObject<AsyncClientStreamImpl<RequestType, ResponseType>> {
public:
  AsyncClientStreamImpl(AsyncClientImpl<RequestType, ResponseType>& parent,
                        AsyncClientCallbacks<ResponseType>& callbacks)
      : parent_(parent), callbacks_(callbacks) {}

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
      Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));

      if (frame.flags_ != GRPC_FH_DEFAULT || !response->ParseFromZeroCopyStream(&stream)) {
        streamError(Status::GrpcStatus::Internal);
        return;
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

  // Grpc::AsyncClientStream
  void sendMessage(const RequestType& request) override {
    stream_->sendData(*Common::serializeBody(request), false);
  }

  void closeStream() override { closeLocal(); }

  void resetStream() override {
    // Both closeLocal() and closeRemote() might self-destruct the object. We don't use these below
    // to avoid sequencing issues.
    local_closed_ |= true;
    remote_closed_ |= true;
    cleanup();
  }

  void set_stream(Http::AsyncClient::Stream* stream) { stream_ = stream; }

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
    if (LinkedObject<AsyncClientStreamImpl<RequestType, ResponseType>>::inserted()) {
      LinkedObject<AsyncClientStreamImpl<RequestType, ResponseType>>::removeFromList(
          parent_.active_streams_);
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

  AsyncClientImpl<RequestType, ResponseType>& parent_;
  AsyncClientCallbacks<ResponseType>& callbacks_;
  bool local_closed_{};
  bool remote_closed_{};
  bool http_reset_{};
  Http::AsyncClient::Stream* stream_{};
  Decoder decoder_;
  // This is a member to avoid reallocation on every onData().
  std::vector<Frame> decoded_frames_;

  friend class AsyncClientImpl<RequestType, ResponseType>;
};

} // namespace Grpc
} // namespace Envoy
