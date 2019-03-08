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

AsyncRequest* AsyncClientImpl::send(const Protobuf::MethodDescriptor& service_method,
                                    const Protobuf::Message& request,
                                    AsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                    const absl::optional<std::chrono::milliseconds>& timeout) {
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
  const absl::optional<std::chrono::milliseconds> no_timeout;
  auto grpc_stream =
      std::make_unique<AsyncStreamImpl>(*this, service_method, callbacks, no_timeout);

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
                                 const absl::optional<std::chrono::milliseconds>& timeout)
    : parent_(parent), service_method_(service_method), callbacks_(callbacks), timeout_(timeout) {}

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
  headers_message_ = Common::prepareHeaders(
      parent_.remote_cluster_name_, service_method_.service()->full_name(), service_method_.name(),
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

  if (end_stream) {
    Http::HeaderMapPtr empty_trailers = std::make_unique<Http::HeaderMapImpl>();
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
  stream_->sendData(*Common::serializeBody(request), end_stream);
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

AsyncRequestImpl::AsyncRequestImpl(AsyncClientImpl& parent,
                                   const Protobuf::MethodDescriptor& service_method,
                                   const Protobuf::Message& request,
                                   AsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                   const absl::optional<std::chrono::milliseconds>& timeout)
    : AsyncStreamImpl(parent, service_method, *this, timeout), request_(request),
      callbacks_(callbacks) {

  current_span_ = parent_span.spawnChild(Tracing::EgressConfig::get(),
                                         "async " + parent.remote_cluster_name_ + " egress",
                                         parent.time_source_.systemTime());
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
