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
                                 const envoy::api::v2::core::GrpcService& config,
                                 TimeSource& time_source)
    : cm_(cm), remote_cluster_name_(config.envoy_grpc().cluster_name()),
      initial_metadata_(config.initial_metadata()), time_source_(time_source) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->resetStream();
  }
}

AsyncRequest* AsyncClientImpl::sendRaw(absl::string_view service_full_name,
                                       absl::string_view method_name, Buffer::InstancePtr&& request,
                                       RawAsyncRequestCallbacks& callbacks,
                                       Tracing::Span& parent_span,
                                       const absl::optional<std::chrono::milliseconds>& timeout) {
  auto* const async_request = new AsyncRequestImpl(
      *this, service_full_name, method_name, std::move(request), callbacks, parent_span, timeout);
  std::unique_ptr<AsyncStreamImpl> grpc_stream{async_request};

  grpc_stream->initialize(true);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return async_request;
}

RawAsyncStream* AsyncClientImpl::startRaw(absl::string_view service_full_name,
                                          absl::string_view method_name,
                                          RawAsyncStreamCallbacks& callbacks) {
  const absl::optional<std::chrono::milliseconds> no_timeout;
  auto grpc_stream = std::make_unique<AsyncStreamImpl>(*this, service_full_name, method_name,
                                                       callbacks, no_timeout);

  grpc_stream->initialize(false);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return active_streams_.front().get();
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, absl::string_view service_full_name,
                                 absl::string_view method_name, RawAsyncStreamCallbacks& callbacks,
                                 const absl::optional<std::chrono::milliseconds>& timeout)
    : parent_(parent), service_full_name_(service_full_name), method_name_(method_name),
      callbacks_(callbacks), timeout_(timeout) {}

void AsyncStreamImpl::initialize(bool buffer_body_for_retry) {
  if (parent_.cm_.get(parent_.remote_cluster_name_) == nullptr) {
    callbacks_.onRemoteClose(Status::GrpcStatus::Unavailable, "Cluster not available");
    http_reset_ = true;
    return;
  }

  auto& http_async_client = parent_.cm_.httpAsyncClientForCluster(parent_.remote_cluster_name_);
  dispatcher_ = &http_async_client.dispatcher();
  stream_ = http_async_client.start(
      *this, Http::AsyncClient::StreamOptions().setTimeout(timeout_).setBufferBodyForRetry(
                 buffer_body_for_retry));

  if (stream_ == nullptr) {
    callbacks_.onRemoteClose(Status::GrpcStatus::Unavailable, EMPTY_STRING);
    http_reset_ = true;
    return;
  }

  // TODO(htuch): match Google gRPC base64 encoding behavior for *-bin headers, see
  // https://github.com/envoyproxy/envoy/pull/2444#discussion_r163914459.
  headers_message_ =
      Common::prepareHeaders(parent_.remote_cluster_name_, service_full_name_, method_name_,
                             absl::optional<std::chrono::milliseconds>(timeout_));
  // Fill service-wide initial metadata.
  for (const auto& header_value : parent_.initial_metadata_) {
    headers_message_->headers().addCopy(Http::LowerCaseString(header_value.key()),
                                        header_value.value());
  }
  callbacks_.onCreateInitialMetadata(headers_message_->headers());
  stream_->sendHeaders(headers_message_->headers(), false);
}

// TODO(htuch): match Google gRPC base64 encoding behavior for *-bin headers, see
// https://github.com/envoyproxy/envoy/pull/2444#discussion_r163914459.
void AsyncStreamImpl::onHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  const auto http_response_status = Http::Utility::getResponseStatus(*headers);
  const auto grpc_status = Common::getGrpcStatus(*headers);
  callbacks_.onReceiveInitialMetadata(end_stream ? std::make_unique<Http::HeaderMapImpl>()
                                                 : std::move(headers));
  if (http_response_status != enumToInt(Http::Code::OK)) {
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md requires that
    // grpc-status be used if available.
    if (end_stream && grpc_status) {
      // There is actually no use-after-move problem here,
      // because it will only be executed when end_stream is equal to true.
      onTrailers(std::move(headers)); // NOLINT(bugprone-use-after-move)
      return;
    }
    // Technically this should be
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md
    // as given by Grpc::Utility::httpToGrpcStatus(), but the Google gRPC client treats
    // this as GrpcStatus::Canceled.
    streamError(Status::GrpcStatus::Canceled);
    return;
  }
  if (end_stream) {
    onTrailers(std::move(headers));
  }
}

void AsyncStreamImpl::onData(Buffer::Instance& data, bool end_stream) {
  decoded_frames_.clear();
  if (!decoder_.decode(data, decoded_frames_)) {
    streamError(Status::GrpcStatus::Internal);
    return;
  }

  for (auto& frame : decoded_frames_) {
    if (frame.length_ > 0 && frame.flags_ != GRPC_FH_DEFAULT) {
      streamError(Status::GrpcStatus::Internal);
      return;
    }
    if (!callbacks_.onReceiveMessageRaw(frame.data_ ? std::move(frame.data_)
                                                    : std::make_unique<Buffer::OwnedImpl>())) {
      streamError(Status::GrpcStatus::Internal);
      return;
    }
  }

  if (end_stream) {
    streamError(Status::GrpcStatus::Unknown);
  }
}

// TODO(htuch): match Google gRPC base64 encoding behavior for *-bin headers, see
// https://github.com/envoyproxy/envoy/pull/2444#discussion_r163914459.
void AsyncStreamImpl::onTrailers(Http::HeaderMapPtr&& trailers) {
  auto grpc_status = Common::getGrpcStatus(*trailers);
  const std::string grpc_message = Common::getGrpcMessage(*trailers);
  callbacks_.onReceiveTrailingMetadata(std::move(trailers));
  if (!grpc_status) {
    grpc_status = Status::GrpcStatus::Unknown;
  }
  callbacks_.onRemoteClose(grpc_status.value(), grpc_message);
  cleanup();
}

void AsyncStreamImpl::streamError(Status::GrpcStatus grpc_status, const std::string& message) {
  callbacks_.onReceiveTrailingMetadata(std::make_unique<Http::HeaderMapImpl>());
  callbacks_.onRemoteClose(grpc_status, message);
  resetStream();
}

void AsyncStreamImpl::onReset() {
  if (http_reset_) {
    return;
  }

  http_reset_ = true;
  streamError(Status::GrpcStatus::Internal);
}

void AsyncStreamImpl::sendMessage(const Protobuf::Message& request, bool end_stream) {
  stream_->sendData(*Common::serializeToGrpcFrame(request), end_stream);
}

void AsyncStreamImpl::sendMessageRaw(Buffer::InstancePtr&& buffer, bool end_stream) {
  Common::prependGrpcFrameHeader(*buffer);
  stream_->sendData(*buffer, end_stream);
}

void AsyncStreamImpl::closeStream() {
  Buffer::OwnedImpl empty_buffer;
  stream_->sendData(empty_buffer, true);
}

void AsyncStreamImpl::resetStream() { cleanup(); }

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

AsyncRequestImpl::AsyncRequestImpl(AsyncClientImpl& parent, absl::string_view service_full_name,
                                   absl::string_view method_name, Buffer::InstancePtr&& request,
                                   RawAsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                   const absl::optional<std::chrono::milliseconds>& timeout)
    : AsyncStreamImpl(parent, service_full_name, method_name, *this, timeout),
      request_(std::move(request)), callbacks_(callbacks) {

  current_span_ = parent_span.spawnChild(Tracing::EgressConfig::get(),
                                         "async " + parent.remote_cluster_name_ + " egress",
                                         parent.time_source_.systemTime());
  current_span_->setTag(Tracing::Tags::get().UpstreamCluster, parent.remote_cluster_name_);
  current_span_->setTag(Tracing::Tags::get().Component, Tracing::Tags::get().Proxy);
}

void AsyncRequestImpl::initialize(bool buffer_body_for_retry) {
  AsyncStreamImpl::initialize(buffer_body_for_retry);
  if (this->hasResetStream()) {
    return;
  }
  this->sendMessageRaw(std::move(request_), true);
}

void AsyncRequestImpl::cancel() {
  current_span_->setTag(Tracing::Tags::get().Status, Tracing::Tags::get().Canceled);
  current_span_->finishSpan();
  this->resetStream();
}

void AsyncRequestImpl::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  current_span_->injectContext(metadata);
  callbacks_.onCreateInitialMetadata(metadata);
}

void AsyncRequestImpl::onReceiveInitialMetadata(Http::HeaderMapPtr&&) {}

bool AsyncRequestImpl::onReceiveMessageRaw(Buffer::InstancePtr&& response) {
  response_ = std::move(response);
  return true;
}

void AsyncRequestImpl::onReceiveTrailingMetadata(Http::HeaderMapPtr&&) {}

void AsyncRequestImpl::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  current_span_->setTag(Tracing::Tags::get().GrpcStatusCode, std::to_string(status));

  if (status != Grpc::Status::GrpcStatus::Ok) {
    current_span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    callbacks_.onFailure(status, message, *current_span_);
  } else if (response_ == nullptr) {
    current_span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
    callbacks_.onFailure(Status::Internal, EMPTY_STRING, *current_span_);
  } else {
    callbacks_.onSuccessRaw(std::move(response_), *current_span_);
  }

  current_span_->finishSpan();
}

} // namespace Grpc
} // namespace Envoy
