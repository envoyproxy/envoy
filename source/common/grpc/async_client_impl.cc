#include "source/common/grpc/async_client_impl.h"

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Grpc {

AsyncClientImpl::AsyncClientImpl(Upstream::ClusterManager& cm,
                                 const envoy::config::core::v3::GrpcService& config,
                                 TimeSource& time_source)
    : max_recv_message_length_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.envoy_grpc(), max_receive_message_length, 0)),
      skip_envoy_headers_(config.envoy_grpc().skip_envoy_headers()), cm_(cm),
      remote_cluster_name_(config.envoy_grpc().cluster_name()),
      host_name_(config.envoy_grpc().authority()), time_source_(time_source),
      metadata_parser_(THROW_OR_RETURN_VALUE(
          Router::HeaderParser::configure(
              config.initial_metadata(),
              envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD),
          Router::HeaderParserPtr)),
      retry_policy_(
          config.has_retry_policy()
              ? absl::optional<envoy::config::route::v3::
                                   RetryPolicy>{Http::Utility::convertCoreToRouteRetryPolicy(
                    config.retry_policy(), "")}
              : absl::nullopt) {}

AsyncClientImpl::~AsyncClientImpl() {
  ASSERT(isThreadSafe());
  while (!active_streams_.empty()) {
    active_streams_.front()->resetStream();
  }
}

AsyncRequest* AsyncClientImpl::sendRaw(absl::string_view service_full_name,
                                       absl::string_view method_name, Buffer::InstancePtr&& request,
                                       RawAsyncRequestCallbacks& callbacks,
                                       Tracing::Span& parent_span,
                                       const Http::AsyncClient::RequestOptions& options) {
  ASSERT(isThreadSafe());
  auto* const async_request = new AsyncRequestImpl(
      *this, service_full_name, method_name, std::move(request), callbacks, parent_span, options);
  AsyncStreamImplPtr grpc_stream{async_request};

  grpc_stream->initialize(true);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  LinkedList::moveIntoList(std::move(grpc_stream), active_streams_);
  return async_request;
}

RawAsyncStream* AsyncClientImpl::startRaw(absl::string_view service_full_name,
                                          absl::string_view method_name,
                                          RawAsyncStreamCallbacks& callbacks,
                                          const Http::AsyncClient::StreamOptions& options) {
  ASSERT(isThreadSafe());
  auto grpc_stream =
      std::make_unique<AsyncStreamImpl>(*this, service_full_name, method_name, callbacks, options);

  grpc_stream->initialize(options.buffer_body_for_retry);
  if (grpc_stream->hasResetStream()) {
    return nullptr;
  }

  LinkedList::moveIntoList(std::move(grpc_stream), active_streams_);
  return active_streams_.front().get();
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, absl::string_view service_full_name,
                                 absl::string_view method_name, RawAsyncStreamCallbacks& callbacks,
                                 const Http::AsyncClient::StreamOptions& options)
    : parent_(parent), service_full_name_(service_full_name), method_name_(method_name),
      callbacks_(callbacks), options_(options) {
  // Apply parent retry policy if no per-stream override.
  if (!options.retry_policy.has_value() && parent_.retryPolicy().has_value()) {
    options_.setRetryPolicy(*parent_.retryPolicy());
  }

  // Apply parent `skip_envoy_headers_` setting from configuration, if no per-stream
  // override. (i.e., no override of default stream option from true to false)
  if (options.send_internal) {
    options_.setSendInternal(!parent_.skip_envoy_headers_);
  }
  if (options.send_xff) {
    options_.setSendXff(!parent_.skip_envoy_headers_);
  }

  // Configure the maximum frame length
  decoder_.setMaxFrameLength(parent_.max_recv_message_length_);

  if (options.parent_span_ != nullptr) {
    const std::string child_span_name =
        options.child_span_name_.empty()
            ? absl::StrCat("async ", service_full_name, ".", method_name, " egress")
            : options.child_span_name_;

    current_span_ = options.parent_span_->spawnChild(Tracing::EgressConfig::get(), child_span_name,
                                                     parent.time_source_.systemTime());
    current_span_->setTag(Tracing::Tags::get().UpstreamCluster, parent.remote_cluster_name_);
    current_span_->setTag(Tracing::Tags::get().UpstreamAddress, parent.host_name_.empty()
                                                                    ? parent.remote_cluster_name_
                                                                    : parent.host_name_);
    current_span_->setTag(Tracing::Tags::get().Component, Tracing::Tags::get().Proxy);
  } else {
    current_span_ = std::make_unique<Tracing::NullSpan>();
  }

  if (options.sampled_.has_value()) {
    current_span_->setSampled(options.sampled_.value());
  }
}

void AsyncStreamImpl::initialize(bool buffer_body_for_retry) {
  const auto thread_local_cluster = parent_.cm_.getThreadLocalCluster(parent_.remote_cluster_name_);
  if (thread_local_cluster == nullptr) {
    notifyRemoteClose(Status::WellKnownGrpcStatus::Unavailable, "Cluster not available");
    http_reset_ = true;
    return;
  }

  cluster_info_ = thread_local_cluster->info();
  auto& http_async_client = thread_local_cluster->httpAsyncClient();
  dispatcher_ = &http_async_client.dispatcher();
  stream_ = http_async_client.start(*this, options_.setBufferBodyForRetry(buffer_body_for_retry));
  if (stream_ == nullptr) {
    notifyRemoteClose(Status::WellKnownGrpcStatus::Unavailable, EMPTY_STRING);
    http_reset_ = true;
    return;
  }

  // TODO(htuch): match Google gRPC base64 encoding behavior for *-bin headers, see
  // https://github.com/envoyproxy/envoy/pull/2444#discussion_r163914459.
  headers_message_ = Common::prepareHeaders(
      parent_.host_name_.empty() ? parent_.remote_cluster_name_ : parent_.host_name_,
      service_full_name_, method_name_, options_.timeout);
  // Fill service-wide initial metadata.
  // TODO(cpakulski): Find a better way to access requestHeaders
  // request headers should not be stored in stream_info.
  // Maybe put it to parent_context?
  // Since request headers may be empty, consider using Envoy::OptRef.
  parent_.metadata_parser_->evaluateHeaders(headers_message_->headers(),
                                            options_.parent_context.stream_info);

  Tracing::HttpTraceContext trace_context(headers_message_->headers());
  Tracing::UpstreamContext upstream_context(nullptr,                         // host_
                                            cluster_info_.get(),             // cluster_
                                            Tracing::ServiceType::EnvoyGrpc, // service_type_
                                            true                             // async_client_span_
  );
  current_span_->injectContext(trace_context, upstream_context);
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
      // NOLINTNEXTLINE(bugprone-use-after-move)
      onTrailers(Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*headers));
      return;
    }
    // Status is translated via Utility::httpToGrpcStatus per
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md
    streamError(Utility::httpToGrpcStatus(http_response_status));
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
  auto status = decoder_.decode(data, decoded_frames_);

  // decode() currently only returns two types of error:
  // - decoding error is mapped to ResourceExhausted
  // - over-limit error is mapped to Internal.
  // Other potential errors in the future are mapped to internal for now.
  if (status.code() == absl::StatusCode::kResourceExhausted) {
    streamError(Status::WellKnownGrpcStatus::ResourceExhausted);
    return;
  }
  if (status.code() != absl::StatusCode::kOk) {
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
  notifyRemoteClose(grpc_status.value(), grpc_message);
  cleanup();
}

void AsyncStreamImpl::streamError(Status::GrpcStatus grpc_status, const std::string& message) {
  callbacks_.onReceiveTrailingMetadata(Http::ResponseTrailerMapImpl::create());
  notifyRemoteClose(grpc_status, message);
  resetStream();
}

void AsyncStreamImpl::notifyRemoteClose(Grpc::Status::GrpcStatus status,
                                        const std::string& message) {
  current_span_->setTag(Tracing::Tags::get().GrpcStatusCode, std::to_string(status));
  if (status != Grpc::Status::WellKnownGrpcStatus::Ok) {
    current_span_->setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
  }
  current_span_->finishSpan();
  callbacks_.onRemoteClose(status, message);
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

void AsyncStreamImpl::sendMessageRaw(Buffer::InstancePtr&& buffer, bool end_stream) {
  Common::prependGrpcFrameHeader(*buffer);
  stream_->sendData(*buffer, end_stream);
}

void AsyncStreamImpl::closeStream() {
  Buffer::OwnedImpl empty_buffer;
  stream_->sendData(empty_buffer, true);
  current_span_->setTag(Tracing::Tags::get().Status, Tracing::Tags::get().Canceled);
  current_span_->finishSpan();
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
    ASSERT(dispatcher_->isThreadSafe());
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

  current_span_ =
      parent_span.spawnChild(Tracing::EgressConfig::get(),
                             absl::StrCat("async ", service_full_name, ".", method_name, " egress"),
                             parent.time_source_.systemTime());
  current_span_->setTag(Tracing::Tags::get().UpstreamCluster, parent.remote_cluster_name_);
  current_span_->setTag(Tracing::Tags::get().UpstreamAddress, parent.host_name_.empty()
                                                                  ? parent.remote_cluster_name_
                                                                  : parent.host_name_);
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

const StreamInfo::StreamInfo& AsyncRequestImpl::streamInfo() const {
  return AsyncStreamImpl::streamInfo();
}

void AsyncRequestImpl::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  Tracing::HttpTraceContext trace_context(metadata);
  Tracing::UpstreamContext upstream_context(nullptr,                         // host_
                                            cluster_info_.get(),             // cluster_
                                            Tracing::ServiceType::EnvoyGrpc, // service_type_
                                            true                             // async_client_span_
  );
  current_span_->injectContext(trace_context, upstream_context);
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
