#include "source/common/http/async_client_impl.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/grpc/common.h"
#include "source/common/http/utility.h"
#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {
AsyncClientImpl::AsyncClientImpl(Upstream::ClusterInfoConstSharedPtr cluster,
                                 Stats::Store& stats_store, Event::Dispatcher& dispatcher,
                                 const LocalInfo::LocalInfo& local_info,
                                 Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                                 Random::RandomGenerator& random,
                                 Router::ShadowWriterPtr&& shadow_writer,
                                 Http::Context& http_context, Router::Context& router_context)
    : singleton_manager_(cm.clusterManagerFactory().singletonManager()), cluster_(cluster),
      config_(http_context.asyncClientStatPrefix(), local_info, *stats_store.rootScope(), cm,
              runtime, random, std::move(shadow_writer), true, false, false, false, false, false,
              {}, dispatcher.timeSource(), http_context, router_context),
      dispatcher_(dispatcher) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->reset();
  }
}

template <typename T> T* AsyncClientImpl::internalStartRequest(T* async_request) {
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
      new AsyncRequestImpl(std::move(request), *this, callbacks, options);
  return internalStartRequest(async_request);
}

AsyncClient::OngoingRequest*
AsyncClientImpl::startRequest(RequestHeaderMapPtr&& request_headers, Callbacks& callbacks,
                              const AsyncClient::RequestOptions& options) {
  AsyncOngoingRequestImpl* async_request =
      new AsyncOngoingRequestImpl(std::move(request_headers), *this, callbacks, options);
  return internalStartRequest(async_request);
}

AsyncClient::Stream* AsyncClientImpl::start(AsyncClient::StreamCallbacks& callbacks,
                                            const AsyncClient::StreamOptions& options) {
  std::unique_ptr<AsyncStreamImpl> new_stream{new AsyncStreamImpl(*this, callbacks, options)};
  LinkedList::moveIntoList(std::move(new_stream), active_streams_);
  return active_streams_.front().get();
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                                 const AsyncClient::StreamOptions& options)
    : parent_(parent), stream_callbacks_(callbacks), stream_id_(parent.config_.random_.random()),
      router_(options.filter_config_ ? *options.filter_config_ : parent.config_,
              parent.config_.async_stats_),
      stream_info_(Protocol::Http11, parent.dispatcher().timeSource(), nullptr),
      tracing_config_(Tracing::EgressConfig::get()),
      route_(std::make_shared<NullRouteImpl>(parent_.cluster_->name(), parent_.singleton_manager_,
                                             options.timeout, options.hash_policy,
                                             options.retry_policy)),
      account_(options.account_), buffer_limit_(options.buffer_limit_),
      send_xff_(options.send_xff) {
  stream_info_.dynamicMetadata().MergeFrom(options.metadata);
  stream_info_.setIsShadow(options.is_shadow);
  stream_info_.setUpstreamClusterInfo(parent_.cluster_);
  stream_info_.route_ = route_;

  if (options.buffer_body_for_retry) {
    buffered_body_ = std::make_unique<Buffer::OwnedImpl>(account_);
  }

  router_.setDecoderFilterCallbacks(*this);
  // TODO(mattklein123): Correctly set protocol in stream info when we support access logging.
}

void AsyncStreamImpl::encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                                    absl::string_view) {
  ENVOY_LOG(debug, "async http request response headers (end_stream={}):\n{}", end_stream,
            *headers);
  ASSERT(!remote_closed_);
  encoded_response_headers_ = true;
  stream_callbacks_.onHeaders(std::move(headers), end_stream);
  closeRemote(end_stream);
  // At present, the router cleans up stream state as soon as the remote is closed, making a
  // half-open local stream unsupported and dangerous. Ensure we close locally to trigger completion
  // and keep things consistent. Another option would be to issue a stream reset here if local isn't
  // yet closed, triggering cleanup along a more standardized path. However, this would require
  // additional logic to handle the response completion and subsequent reset, and run the risk of
  // being interpreted as a failure, when in fact no error has necessarily occurred. Gracefully
  // closing seems most in-line with behavior elsewhere in Envoy for now.
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
  headers.setReferenceEnvoyInternalRequest(Headers::get().EnvoyInternalRequestValues.True);
  if (send_xff_) {
    Utility::appendXff(headers, *parent_.config_.local_info_.address());
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
  router_.onDestroy();
  resetStream();
}

void AsyncStreamImpl::cleanup() {
  ASSERT(dispatcher().isThreadSafe());
  local_closed_ = remote_closed_ = true;
  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (inserted()) {
    dispatcher().deferredDelete(removeFromList(parent_.active_streams_));
  }
}

void AsyncStreamImpl::resetStream(Http::StreamResetReason, absl::string_view) {
  stream_callbacks_.onReset();
  cleanup();
}

AsyncRequestSharedImpl::AsyncRequestSharedImpl(AsyncClientImpl& parent,
                                               AsyncClient::Callbacks& callbacks,
                                               const AsyncClient::RequestOptions& options)
    : AsyncStreamImpl(parent, *this, options), callbacks_(callbacks) {
  if (nullptr != options.parent_span_) {
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
  child_span_->injectContext(request_->headers(), nullptr);
  sendHeaders(request_->headers(), request_->body().length() == 0);
  if (request_->body().length() != 0) {
    // It's possible this will be a no-op due to a local response synchronously generated in
    // sendHeaders; guards handle this within AsyncStreamImpl.
    sendData(request_->body(), true);
  }
  // TODO(mattklein123): Support request trailers.
}

void AsyncOngoingRequestImpl::initialize() {
  child_span_->injectContext(*request_headers_, nullptr);
  sendHeaders(*request_headers_, false);
}

void AsyncRequestSharedImpl::onComplete() {
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

void AsyncRequestSharedImpl::onData(Buffer::Instance& data, bool) { response_->body().move(data); }

void AsyncRequestSharedImpl::onTrailers(ResponseTrailerMapPtr&& trailers) {
  response_->trailers(std::move(trailers));
}

void AsyncRequestSharedImpl::onReset() {
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
    // In this case we don't have a valid response so we do need to raise a failure.
    callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
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
