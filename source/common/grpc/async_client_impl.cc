#include "common/grpc/async_client_impl.h"

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Grpc {

AsyncClientImpl::AsyncClientImpl(Upstream::ClusterManager& cm,
                                 const envoy::config::core::v3::GrpcService& config,
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
                                       const Http::AsyncClient::RequestOptions& options) {
  auto* const async_request = new AsyncRequestImpl(
      *this, service_full_name, method_name, std::move(request), callbacks, parent_span, options);
  AsyncStreamImplPtr grpc_stream{async_request};

  grpc_stream->initialize(true);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return async_request;
}

RawAsyncStream* AsyncClientImpl::startRaw(absl::string_view service_full_name,
                                          absl::string_view method_name,
                                          RawAsyncStreamCallbacks& callbacks,
                                          const Http::AsyncClient::StreamOptions& options) {
  auto grpc_stream =
      std::make_unique<AsyncStreamImpl>(*this, service_full_name, method_name, callbacks, options);

  grpc_stream->initialize(false);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  grpc_stream->moveIntoList(std::move(grpc_stream), active_streams_);
  return active_streams_.front().get();
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, absl::string_view service_full_name,
                                 absl::string_view method_name, RawAsyncStreamCallbacks& callbacks,
                                 const Http::AsyncClient::StreamOptions& options)
    : parent_(parent), service_full_name_(service_full_name), method_name_(method_name),
      callbacks_(callbacks), options_(options) {}

void AsyncStreamImpl::initialize(bool buffer_body_for_retry) {
  if (parent_.cm_.get(parent_.remote_cluster_name_) == nullptr) {
    callbacks_.onRemoteClose(Status::WellKnownGrpcStatus::Unavailable, "Cluster not available");
    http_reset_ = true;
    return;
  }

  auto& http_async_client = parent_.cm_.httpAsyncClientForCluster(parent_.remote_cluster_name_);
  dispatcher_ = &http_async_client.dispatcher();
  stream_ = http_async_client.start(*this, options_.setBufferBodyForRetry(buffer_body_for_retry));

  if (stream_ == nullptr) {
    callbacks_.onRemoteClose(Status::WellKnownGrpcStatus::Unavailable, EMPTY_STRING);
    http_reset_ = true;
    return;
  }

  // TODO(htuch): match Google gRPC base64 encoding behavior for *-bin headers, see
  // https://github.com/envoyproxy/envoy/pull/2444#discussion_r163914459.
  headers_message_ =
      Common::prepareHeaders(parent_.remote_cluster_name_, service_full_name_, method_name_,
                             absl::optional<std::chrono::milliseconds>(options_.timeout));
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
void AsyncStreamImpl::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  const auto http_response_status = Http::Utility::getResponseStatus(*headers);
  const auto grpc_status = Common::getGrpcStatus(*headers);
  callbacks_.onReceiveInitialMetadata(end_stream ? Http::ResponseHeaderMapImpl::create()
                                                 : std::move(headers));
  if (http_response_status != enumToInt(Http::Code::OK)) {
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md requires that
    // grpc-status be used if available.
    if (end_stream && grpc_status) {
      // Due to headers/trailers type differences we need to copy here. This is an uncommon case but
      // we can potentially optimize in the future.

      // TODO(mattklein123): clang-tidy is showing a use after move when passing to
      // onReceiveInitialMetadata() above. This looks like an actual bug that I will fix in a
      // follow up.
      onTrailers(Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*headers));
      return;
    }
    // Technically this should be
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md
    // as given by Grpc::Utility::httpToGrpcStatus(), but the Google gRPC client treats
    // this as WellKnownGrpcStatus::Canceled.
    streamError(Status::WellKnownGrpcStatus::Canceled);
    return;
  }
  if (end_stream) {
    // Due to headers/trailers type differences we need to copy here. This is an uncommon case but
    // we can potentially optimize in the future.
    onTrailers(Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*headers));
  }
}

void AsyncStreamImpl::onData(Buffer::Instance& data, bool end_stream) {
  decoded_frames_.clear();
  if (!decoder_.decode(data, decoded_frames_)) {
    streamError(Status::WellKnownGrpcStatus::Internal);
    return;
  }

  for (auto& frame : decoded_frames_) {
    if (frame.length_ > 0 && frame.flags_ != GRPC_FH_DEFAULT) {
      streamError(Status::WellKnownGrpcStatus::Internal);
      return;
    }
    if (!callbacks_.onReceiveMessageRaw(frame.data_ ? std::move(frame.data_)
                                                    : std::make_unique<Buffer::OwnedImpl>())) {
      streamError(Status::WellKnownGrpcStatus::Internal);
      return;
    }
  }

  if (end_stream) {
    streamError(Status::WellKnownGrpcStatus::Unknown);
  }
}

// TODO(htuch): match Google gRPC base64 encoding behavior for *-bin headers, see
// https://github.com/envoyproxy/envoy/pull/2444#discussion_r163914459.
void AsyncStreamImpl::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  auto grpc_status = Common::getGrpcStatus(*trailers);
  const std::string grpc_message = Common::getGrpcMessage(*trailers);
  callbacks_.onReceiveTrailingMetadata(std::move(trailers));
  if (!grpc_status) {
    grpc_status = Status::WellKnownGrpcStatus::Unknown;
  }
  callbacks_.onRemoteClose(grpc_status.value(), grpc_message);
  cleanup();
}

void AsyncStreamImpl::streamError(Status::GrpcStatus grpc_status, const std::string& message) {
  callbacks_.onReceiveTrailingMetadata(Http::ResponseTrailerMapImpl::create());
  callbacks_.onRemoteClose(grpc_status, message);
  resetStream();
}

void AsyncStreamImpl::onComplete() {
  // No-op since stream completion is handled within other callbacks.
}

void AsyncStreamImpl::onReset() {
  if (http_reset_) {
    return;
  }

  http_reset_ = true;
  streamError(Status::WellKnownGrpcStatus::Internal);
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
                                   const Http::AsyncClient::RequestOptions& options)
    : AsyncStreamImpl(parent, service_full_name, method_name, *this, options),
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

void AsyncRequestImpl::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  current_span_->injectContext(metadata);
  callbacks_.onCreateInitialMetadata(metadata);
}

void AsyncRequestImpl::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) {}

bool AsyncRequestImpl::onReceiveMessageRaw(Buffer::InstancePtr&& response) {
  response_ = std::move(response);
  return true;
}

void AsyncRequestImpl::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) {}

void AsyncRequestImpl::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  current_span_->setTag(Tracing::Tags::get().GrpcStatusCode, std::to_string(status));

  if (status != Grpc::Status::WellKnownGrpcStatus::Ok) {
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
