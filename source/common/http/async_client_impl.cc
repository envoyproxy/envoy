#include "source/common/http/async_client_impl.h"

#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/router/router.h"

#include "source/common/grpc/common.h"
#include "source/common/http/null_route_impl.h"
#include "source/common/http/utility.h"
#include "source/common/local_reply/local_reply.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/upstream/retry_factory.h"

namespace Envoy {
namespace Http {

const absl::string_view AsyncClientImpl::ResponseBufferLimit = "http.async_response_buffer_limit";

AsyncClientImpl::AsyncClientImpl(Upstream::ClusterInfoConstSharedPtr cluster,
                                 Stats::Store& stats_store, Event::Dispatcher& dispatcher,
                                 Upstream::ClusterManager& cm,
                                 Server::Configuration::CommonFactoryContext& factory_context,
                                 Router::ShadowWriterPtr&& shadow_writer,
                                 Http::Context& http_context, Router::Context& router_context)
    : factory_context_(factory_context), cluster_(cluster),
      config_(std::make_shared<Router::FilterConfig>(
          factory_context, http_context.asyncClientStatPrefix(), factory_context.localInfo(),
          *stats_store.rootScope(), cm, factory_context.runtime(),
          factory_context.api().randomGenerator(), std::move(shadow_writer), true, false, false,
          false, false, false, Protobuf::RepeatedPtrField<std::string>{}, dispatcher.timeSource(),
          http_context, router_context)),
      dispatcher_(dispatcher), runtime_(factory_context.runtime()),
      local_reply_(LocalReply::Factory::createDefault()) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->reset();
  }
}

template <typename T> T* AsyncClientImpl::internalStartRequest(T* async_request) {
  if (!async_request) {
    return nullptr;
  }
  async_request->initialize();
  std::unique_ptr<AsyncStreamImpl> new_request{async_request};

  // The request may get immediately failed. If so, we will return nullptr.
  if (!new_request->remote_closed_) {
    LinkedList::moveIntoList(std::move(new_request), active_streams_);
    return async_request;
  } else {
    new_request->cleanup();
    return nullptr;
  }
}

template AsyncRequestImpl*
AsyncClientImpl::internalStartRequest<AsyncRequestImpl>(AsyncRequestImpl*);
template AsyncOngoingRequestImpl*
AsyncClientImpl::internalStartRequest<AsyncOngoingRequestImpl>(AsyncOngoingRequestImpl*);

AsyncClient::Request* AsyncClientImpl::send(RequestMessagePtr&& request,
                                            AsyncClient::Callbacks& callbacks,
                                            const AsyncClient::RequestOptions& options) {
  AsyncRequestImpl* async_request =
      AsyncRequestImpl::create(std::move(request), *this, callbacks, options);
  return internalStartRequest(async_request);
}

AsyncClient::OngoingRequest*
AsyncClientImpl::startRequest(RequestHeaderMapPtr&& request_headers, Callbacks& callbacks,
                              const AsyncClient::RequestOptions& options) {
  AsyncOngoingRequestImpl* async_request =
      AsyncOngoingRequestImpl::create(std::move(request_headers), *this, callbacks, options);
  return internalStartRequest(async_request);
}

AsyncClient::Stream* AsyncClientImpl::start(AsyncClient::StreamCallbacks& callbacks,
                                            const AsyncClient::StreamOptions& options) {
  auto stream_or_error = AsyncStreamImpl::create(*this, callbacks, options);
  if (!stream_or_error.ok()) {
    callbacks.onReset();
    return nullptr;
  }
  LinkedList::moveIntoList(std::move(stream_or_error.value()), active_streams_);
  return active_streams_.front().get();
}

std::unique_ptr<const Router::RetryPolicy>
createRetryPolicy(AsyncClientImpl& parent, const AsyncClient::StreamOptions& options,
                  Server::Configuration::CommonFactoryContext& context,
                  absl::Status& creation_status) {
  if (options.retry_policy.has_value()) {
    Upstream::RetryExtensionFactoryContextImpl factory_context(
        parent.factory_context_.singletonManager());
    auto policy_or_error = Router::RetryPolicyImpl::create(
        options.retry_policy.value(), ProtobufMessage::getNullValidationVisitor(), factory_context,
        context);
    creation_status = policy_or_error.status();
    return policy_or_error.status().ok() ? std::move(policy_or_error.value()) : nullptr;
  }
  if (options.parsed_retry_policy == nullptr) {
    return std::make_unique<Router::RetryPolicyImpl>();
  }
  return nullptr;
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                                 const AsyncClient::StreamOptions& options,
                                 absl::Status& creation_status)
    : parent_(parent), discard_response_body_(options.discard_response_body),
      stream_callbacks_(callbacks), stream_id_(parent.config_->random_.random()),
      router_(options.filter_config_ ? options.filter_config_ : parent.config_,
              parent.config_->async_stats_),
      stream_info_(Protocol::Http11, parent.dispatcher().timeSource(), nullptr,
                   options.filter_state != nullptr
                       ? options.filter_state
                       : std::make_shared<StreamInfo::FilterStateImpl>(
                             StreamInfo::FilterState::LifeSpan::FilterChain)),
      tracing_config_(Tracing::EgressConfig::get()), local_reply_(*parent.local_reply_),
      retry_policy_(createRetryPolicy(parent, options, parent_.factory_context_, creation_status)),
      route_(std::make_shared<NullRouteImpl>(
          parent_.cluster_->name(),
          retry_policy_ != nullptr ? *retry_policy_ : *options.parsed_retry_policy,
          parent_.factory_context_.regexEngine(), options.timeout, options.hash_policy)),
      account_(options.account_), buffer_limit_(options.buffer_limit_), send_xff_(options.send_xff),
      send_internal_(options.send_internal) {
  stream_info_.dynamicMetadata().MergeFrom(options.metadata);
  stream_info_.setIsShadow(options.is_shadow);
  stream_info_.setUpstreamClusterInfo(parent_.cluster_);
  stream_info_.route_ = route_;

  if (options.parent_context.stream_info != nullptr) {
    stream_info_.setParentStreamInfo(*options.parent_context.stream_info);
  }

  if (options.buffer_body_for_retry) {
    buffered_body_ = std::make_unique<Buffer::OwnedImpl>(account_);
  }

  router_.setDecoderFilterCallbacks(*this);
  // TODO(mattklein123): Correctly set protocol in stream info when we support access logging.
}

void AsyncStreamImpl::sendLocalReply(Code code, absl::string_view body,
                                     std::function<void(ResponseHeaderMap& headers)> modify_headers,
                                     const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                                     absl::string_view details) {
  if (encoded_response_headers_) {
    resetStream();
    return;
  }
  Utility::sendLocalReply(
      remote_closed_,
      Utility::EncodeFunctions{
          [modify_headers](ResponseHeaderMap& headers) -> void {
            if (modify_headers != nullptr) {
              modify_headers(headers);
            }
          },
          [this](ResponseHeaderMap& response_headers, Code& code, std::string& body,
                 absl::string_view& content_type) -> void {
            local_reply_.rewrite(request_headers_, response_headers, stream_info_, code, body,
                                 content_type);
          },
          [this, &details](ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
            encodeHeaders(std::move(headers), end_stream, details);
          },
          [this](Buffer::Instance& data, bool end_stream) -> void {
            encodeData(data, end_stream);
          }},
      Utility::LocalReplyData{is_grpc_request_, code, body, grpc_status, is_head_request_});
}
void AsyncStreamImpl::encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                                    absl::string_view) {
  ENVOY_LOG(debug, "async http request response headers (end_stream={}):\n{}", end_stream,
            *headers);
  ASSERT(!remote_closed_);
  encoded_response_headers_ = true;
  stream_callbacks_.onHeaders(std::move(headers), end_stream);
  closeRemote(end_stream);
  // At present, the AsyncStream is always fully closed when the server half closes the stream.
  //
  // Always ensure we close locally to trigger completion. Another option would be to issue a stream
  // reset here if local isn't yet closed, triggering cleanup along a more standardized path.
  // However, this would require additional logic to handle the response completion and subsequent
  // reset, and run the risk of being interpreted as a failure, when in fact no error has
  // necessarily occurred. Gracefully closing seems most in-line with behavior elsewhere in Envoy
  // for now.
  closeLocal(end_stream);
}

void AsyncStreamImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "async http request response data (length={} end_stream={})", data.length(),
            end_stream);
  ASSERT(!remote_closed_);
  stream_callbacks_.onData(data, end_stream);
  closeRemote(end_stream);
  // Ensure we close locally on receiving a complete response; see comment in encodeHeaders for
  // rationale.
  closeLocal(end_stream);
}

void AsyncStreamImpl::encodeTrailers(ResponseTrailerMapPtr&& trailers) {
  ENVOY_LOG(debug, "async http request response trailers:\n{}", *trailers);
  ASSERT(!remote_closed_);
  stream_callbacks_.onTrailers(std::move(trailers));
  closeRemote(true);
  // Ensure we close locally on receiving a complete response; see comment in encodeHeaders for
  // rationale.
  closeLocal(true);
}

void AsyncStreamImpl::sendHeaders(RequestHeaderMap& headers, bool end_stream) {
  request_headers_ = &headers;

  if (Http::Headers::get().MethodValues.Head == headers.getMethodValue()) {
    is_head_request_ = true;
  }

  is_grpc_request_ = Grpc::Common::isGrpcRequestHeaders(headers);
  if (send_internal_) {
    headers.setReferenceEnvoyInternalRequest(Headers::get().EnvoyInternalRequestValues.True);
  }

  if (send_xff_) {
    Utility::appendXff(headers, *parent_.config_->local_info_.address());
  }

  router_.decodeHeaders(headers, end_stream);
  closeLocal(end_stream);
}

void AsyncStreamImpl::sendData(Buffer::Instance& data, bool end_stream) {
  ASSERT(dispatcher().isThreadSafe());
  // Map send calls after local closure to no-ops. The send call could have been queued prior to
  // remote reset or closure, and/or closure could have occurred synchronously in response to a
  // previous send. In these cases the router will have already cleaned up stream state. This
  // parallels handling in the main Http::ConnectionManagerImpl as well.
  if (local_closed_) {
    return;
  }

  if (buffered_body_ != nullptr) {
    // TODO(shikugawa): Currently, data is dropped when the retry buffer overflows and there is no
    // ability implement any error handling. We need to implement buffer overflow handling in the
    // future. Options include configuring the max buffer size, or for use cases like gRPC
    // streaming, deleting old data in the retry buffer.
    if (buffered_body_->length() + data.length() > kBufferLimitForRetry) {
      ENVOY_LOG_EVERY_POW_2(
          warn, "the buffer size limit (64KB) for async client retries has been exceeded.");
    } else {
      buffered_body_->add(data);
    }
  }

  router_.decodeData(data, end_stream);
  closeLocal(end_stream);
}

void AsyncStreamImpl::sendTrailers(RequestTrailerMap& trailers) {
  request_trailers_ = &trailers;

  ASSERT(dispatcher().isThreadSafe());
  // See explanation in sendData.
  if (local_closed_) {
    return;
  }

  router_.decodeTrailers(trailers);
  closeLocal(true);
}

void AsyncStreamImpl::closeLocal(bool end_stream) {
  // This guard ensures that we don't attempt to clean up a stream or fire a completion callback
  // for a stream that has already been closed. Both send* calls and resets can result in stream
  // closure, and this state may be updated synchronously during stream interaction and callbacks.
  // Additionally AsyncRequestImpl maintains behavior wherein its onComplete callback will fire
  // immediately upon receiving a complete response, regardless of whether it has finished sending
  // a request.
  // Previous logic treated post-closure entry here as more-or-less benign (providing later-stage
  // guards against redundant cleanup), but to surface consistent stream state via callbacks,
  // it's necessary to be more rigorous.
  // TODO(goaway): Consider deeper cleanup of assumptions here.
  if (local_closed_) {
    return;
  }

  local_closed_ = end_stream;
  if (complete()) {
    stream_callbacks_.onComplete();
    cleanup();
  }
}

void AsyncStreamImpl::closeRemote(bool end_stream) {
  // This guard ensures that we don't attempt to clean up a stream or fire a completion callback for
  // a stream that has already been closed. This function is called synchronously after callbacks
  // have executed, and it's possible for callbacks to, for instance, directly reset a stream or
  // close the remote manually. The test case ResetInOnHeaders covers this case specifically.
  // Previous logic treated post-closure entry here as more-or-less benign (providing later-stage
  // guards against redundant cleanup), but to surface consistent stream state via callbacks, it's
  // necessary to be more rigorous.
  // TODO(goaway): Consider deeper cleanup of assumptions here.
  if (remote_closed_) {
    return;
  }

  remote_closed_ = end_stream;
  if (complete()) {
    stream_callbacks_.onComplete();
    cleanup();
  }
}

void AsyncStreamImpl::reset() {
  routerDestroy();
  resetStream();
}

void AsyncStreamImpl::routerDestroy() {
  if (!router_destroyed_) {
    router_destroyed_ = true;
    router_.onDestroy();
  }
}

void AsyncStreamImpl::cleanup() {
  ASSERT(dispatcher().isThreadSafe());
  local_closed_ = remote_closed_ = true;
  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (inserted()) {
    routerDestroy();
    dispatcher().deferredDelete(removeFromList(parent_.active_streams_));
  }
}

void AsyncStreamImpl::resetStream(Http::StreamResetReason, absl::string_view) {
  stream_callbacks_.onReset();
  cleanup();
}

AsyncRequestSharedImpl::AsyncRequestSharedImpl(AsyncClientImpl& parent,
                                               AsyncClient::Callbacks& callbacks,
                                               const AsyncClient::RequestOptions& options,
                                               absl::Status& creation_status)
    : AsyncStreamImpl(parent, *this, options, creation_status), callbacks_(callbacks),
      response_buffer_limit_(parent.runtime_.snapshot().getInteger(
          AsyncClientImpl::ResponseBufferLimit, kBufferLimitForResponse)) {
  if (!creation_status.ok()) {
    return;
  }
  if (options.parent_span_ != nullptr) {
    const std::string child_span_name =
        options.child_span_name_.empty()
            ? absl::StrCat("async ", parent.cluster_->name(), " egress")
            : options.child_span_name_;
    child_span_ = options.parent_span_->spawnChild(Tracing::EgressConfig::get(), child_span_name,
                                                   parent.dispatcher().timeSource().systemTime());
  } else {
    child_span_ = std::make_unique<Tracing::NullSpan>();
  }
  // Span gets sampled by default, as sampled_ defaults to true.
  // If caller overrides sampled_ with empty value, sampling status of the parent is kept.
  if (options.sampled_.has_value()) {
    child_span_->setSampled(options.sampled_.value());
  }
}

void AsyncRequestImpl::initialize() {
  Tracing::HttpTraceContext trace_context(request_->headers());
  Tracing::UpstreamContext upstream_context(nullptr,                    // host_
                                            parent_.cluster_.get(),     // cluster_
                                            Tracing::ServiceType::Http, // service_type_
                                            true                        // async_client_span_
  );
  child_span_->injectContext(trace_context, upstream_context);
  sendHeaders(request_->headers(), request_->body().length() == 0);
  if (request_->body().length() != 0) {
    // It's possible this will be a no-op due to a local response synchronously generated in
    // sendHeaders; guards handle this within AsyncStreamImpl.
    sendData(request_->body(), true);
  }
  // TODO(mattklein123): Support request trailers.
}

void AsyncOngoingRequestImpl::initialize() {
  Tracing::HttpTraceContext trace_context(*request_headers_);
  Tracing::UpstreamContext upstream_context(nullptr,                    // host_
                                            parent_.cluster_.get(),     // cluster_
                                            Tracing::ServiceType::Http, // service_type_
                                            true                        // async_client_span_
  );
  child_span_->injectContext(trace_context, upstream_context);
  sendHeaders(*request_headers_, false);
}

void AsyncRequestSharedImpl::onComplete() {
  complete_ = true;
  callbacks_.onBeforeFinalizeUpstreamSpan(*child_span_, &response_->headers());

  Tracing::HttpTracerUtility::finalizeUpstreamSpan(*child_span_, streamInfo(),
                                                   Tracing::EgressConfig::get());

  callbacks_.onSuccess(*this, std::move(response_));
}

void AsyncRequestSharedImpl::onHeaders(ResponseHeaderMapPtr&& headers, bool) {
  const uint64_t response_code = Http::Utility::getResponseStatus(*headers);
  streamInfo().setResponseCode(response_code);
  response_ = std::make_unique<ResponseMessageImpl>(std::move(headers));
}

void AsyncRequestSharedImpl::onData(Buffer::Instance& data, bool) {
  if (discard_response_body_) {
    data.drain(data.length());
    return;
  }

  if (response_->body().length() + data.length() > response_buffer_limit_) {
    ENVOY_LOG_EVERY_POW_2(warn, "the buffer size limit for async client response body "
                                "has been exceeded, draining data");
    data.drain(data.length());
    response_buffer_overlimit_ = true;
    reset();
  } else {
    response_->body().move(data);
  }
}

void AsyncRequestSharedImpl::onTrailers(ResponseTrailerMapPtr&& trailers) {
  response_->trailers(std::move(trailers));
}

void AsyncRequestSharedImpl::onReset() {
  if (complete_) {
    // This request has already been completed; a reset should be ignored.
    return;
  }

  if (!cancelled_) {
    // Set "error reason" tag related to reset. The tagging for "error true" is done inside the
    // Tracing::HttpTracerUtility::finalizeUpstreamSpan.
    child_span_->setTag(Tracing::Tags::get().ErrorReason, "Reset");
  }

  callbacks_.onBeforeFinalizeUpstreamSpan(*child_span_,
                                          remoteClosed() ? &response_->headers() : nullptr);

  // Finalize the span based on whether we received a response or not.
  Tracing::HttpTracerUtility::finalizeUpstreamSpan(*child_span_, streamInfo(),
                                                   Tracing::EgressConfig::get());

  if (!cancelled_) {
    if (response_buffer_overlimit_) {
      callbacks_.onFailure(*this, AsyncClient::FailureReason::ExceedResponseBufferLimit);
    } else {
      // In this case we don't have a valid response so we do need to raise a failure.
      callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
    }
  }
}

void AsyncRequestSharedImpl::cancel() {
  cancelled_ = true;

  // Add tags about the cancellation.
  child_span_->setTag(Tracing::Tags::get().Canceled, Tracing::Tags::get().True);

  reset();
}

} // namespace Http
} // namespace Envoy
