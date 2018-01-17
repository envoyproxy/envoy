#include "common/grpc/async_client_impl.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Grpc {

AsyncClientImpl::AsyncClientImpl(Upstream::ClusterManager& cm,
                                 const std::string& remote_cluster_name)
    : cm_(cm), remote_cluster_name_(remote_cluster_name) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->resetStream();
  }
}

AsyncRequest* AsyncClientImpl::send(const Protobuf::MethodDescriptor& service_method,
                                    const Protobuf::Message& request,
                                    AsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                    const Optional<std::chrono::milliseconds>& timeout) {
  auto* const async_request =
      new AsyncRequestImpl(*this, service_method, request, callbacks, parent_span, timeout);
  std::unique_ptr<AsyncStreamImpl> grpc_stream{async_request};

  grpc_stream->initialize(true);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return async_request;
}

AsyncStream* AsyncClientImpl::start(const Protobuf::MethodDescriptor& service_method,
                                    AsyncStreamCallbacks& callbacks) {
  const Optional<std::chrono::milliseconds> no_timeout;
  std::unique_ptr<AsyncStreamImpl> grpc_stream{
      new AsyncStreamImpl(*this, service_method, callbacks, no_timeout)};

  grpc_stream->initialize(false);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return active_streams_.front().get();
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent,
                                 const Protobuf::MethodDescriptor& service_method,
                                 AsyncStreamCallbacks& callbacks,
                                 const Optional<std::chrono::milliseconds>& timeout)
    : parent_(parent), service_method_(service_method), callbacks_(callbacks), timeout_(timeout) {}

void AsyncStreamImpl::initialize(bool buffer_body_for_retry) {
  auto& http_async_client = parent_.cm_.httpAsyncClientForCluster(parent_.remote_cluster_name_);
  dispatcher_ = &http_async_client.dispatcher();
  stream_ = http_async_client.start(*this, Optional<std::chrono::milliseconds>(timeout_),
                                    buffer_body_for_retry);

  if (stream_ == nullptr) {
    callbacks_.onRemoteClose(Status::GrpcStatus::Unavailable, EMPTY_STRING);
    http_reset_ = true;
    return;
  }

  headers_message_ = Common::prepareHeaders(
      parent_.remote_cluster_name_, service_method_.service()->full_name(), service_method_.name());
  callbacks_.onCreateInitialMetadata(headers_message_->headers());
  stream_->sendHeaders(headers_message_->headers(), false);
}

void AsyncStreamImpl::onHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
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

void AsyncStreamImpl::onData(Buffer::Instance& data, bool end_stream) {
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
    ProtobufTypes::MessagePtr response = callbacks_.createEmptyResponse();
    // TODO(htuch): Need to add support for compressed responses as well here.
    if (frame.length_ > 0) {
      Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));

      if (frame.flags_ != GRPC_FH_DEFAULT || !response->ParseFromZeroCopyStream(&stream)) {
        streamError(Status::GrpcStatus::Internal);
        return;
      }
    }
    callbacks_.onReceiveMessageUntyped(std::move(response));
  }
}

void AsyncStreamImpl::onTrailers(Http::HeaderMapPtr&& trailers) {
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

void AsyncStreamImpl::onReset() {
  if (http_reset_) {
    return;
  }

  http_reset_ = true;
  streamError(Status::GrpcStatus::Internal);
}

void AsyncStreamImpl::sendMessage(const Protobuf::Message& request, bool end_stream) {
  stream_->sendData(*Common::serializeBody(request), end_stream);
  if (end_stream) {
    closeLocal();
  }
}

void AsyncStreamImpl::closeStream() {
  Buffer::OwnedImpl empty_buffer;
  stream_->sendData(empty_buffer, true);
  closeLocal();
}

void AsyncStreamImpl::resetStream() {
  // Both closeLocal() and closeRemote() might self-destruct the object. We don't use these below
  // to avoid sequencing issues.
  local_closed_ |= true;
  remote_closed_ |= true;
  cleanup();
}

void AsyncStreamImpl::cleanup() {
  if (!http_reset_) {
    http_reset_ = true;
    stream_->reset();
  }

  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (LinkedObject<AsyncStreamImpl>::inserted()) {
    dispatcher_->deferredDelete(
        LinkedObject<AsyncStreamImpl>::removeFromList(parent_.active_streams_));
  }
}

AsyncRequestImpl::AsyncRequestImpl(AsyncClientImpl& parent,
                                   const Protobuf::MethodDescriptor& service_method,
                                   const Protobuf::Message& request,
                                   AsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                   const Optional<std::chrono::milliseconds>& timeout)
    : AsyncStreamImpl(parent, service_method, *this, timeout), request_(request),
      callbacks_(callbacks) {

  current_span_ = parent_span.spawnChild(Tracing::EgressConfig::get(),
                                         "async " + parent.remote_cluster_name_ + " egress",
                                         ProdSystemTimeSource::instance_.currentTime());
  current_span_->setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, parent.remote_cluster_name_);
  current_span_->setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY);
}

void AsyncRequestImpl::initialize(bool buffer_body_for_retry) {
  AsyncStreamImpl::initialize(buffer_body_for_retry);
  if (this->hasResetStream()) {
    return;
  }
  this->sendMessage(request_, true);
}

void AsyncRequestImpl::cancel() {
  current_span_->setTag(Tracing::Tags::get().STATUS, Tracing::Tags::get().CANCELED);
  current_span_->finishSpan();
  this->resetStream();
}

ProtobufTypes::MessagePtr AsyncRequestImpl::createEmptyResponse() {
  return callbacks_.createEmptyResponse();
}

void AsyncRequestImpl::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  current_span_->injectContext(metadata);
  callbacks_.onCreateInitialMetadata(metadata);
}

void AsyncRequestImpl::onReceiveInitialMetadata(Http::HeaderMapPtr&&) {}

void AsyncRequestImpl::onReceiveMessageUntyped(ProtobufTypes::MessagePtr&& message) {
  response_ = std::move(message);
}

void AsyncRequestImpl::onReceiveTrailingMetadata(Http::HeaderMapPtr&&) {}

void AsyncRequestImpl::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  current_span_->setTag(Tracing::Tags::get().GRPC_STATUS_CODE, std::to_string(status));

  if (status != Grpc::Status::GrpcStatus::Ok) {
    current_span_->setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE);
    callbacks_.onFailure(status, message, *current_span_);
  } else if (response_ == nullptr) {
    current_span_->setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE);
    callbacks_.onFailure(Status::Internal, EMPTY_STRING, *current_span_);
  } else {
    callbacks_.onSuccessUntyped(std::move(response_), *current_span_);
  }

  current_span_->finishSpan();
}

} // namespace Grpc
} // namespace Envoy
