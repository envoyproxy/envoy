#include "common/http/async_client_impl.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "common/grpc/common.h"
#include "common/http/utility.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
    AsyncStreamImpl::NullRateLimitPolicy::rate_limit_policy_entry_;
const AsyncStreamImpl::NullHedgePolicy AsyncStreamImpl::RouteEntryImpl::hedge_policy_;
const AsyncStreamImpl::NullRateLimitPolicy AsyncStreamImpl::RouteEntryImpl::rate_limit_policy_;
const AsyncStreamImpl::NullRetryPolicy AsyncStreamImpl::RouteEntryImpl::retry_policy_;
const Router::InternalRedirectPolicyImpl AsyncStreamImpl::RouteEntryImpl::internal_redirect_policy_;
const std::vector<Router::ShadowPolicyPtr> AsyncStreamImpl::RouteEntryImpl::shadow_policies_;
const AsyncStreamImpl::NullVirtualHost AsyncStreamImpl::RouteEntryImpl::virtual_host_;
const AsyncStreamImpl::NullRateLimitPolicy AsyncStreamImpl::NullVirtualHost::rate_limit_policy_;
const AsyncStreamImpl::NullConfig AsyncStreamImpl::NullVirtualHost::route_configuration_;
const std::multimap<std::string, std::string> AsyncStreamImpl::RouteEntryImpl::opaque_config_;
const envoy::config::core::v3::Metadata AsyncStreamImpl::RouteEntryImpl::metadata_;
const Config::TypedMetadataImpl<Envoy::Config::TypedMetadataFactory>
    AsyncStreamImpl::RouteEntryImpl::typed_metadata_({});
const AsyncStreamImpl::NullPathMatchCriterion
    AsyncStreamImpl::RouteEntryImpl::path_match_criterion_;
const absl::optional<envoy::config::route::v3::RouteAction::UpgradeConfig::ConnectConfig>
    AsyncStreamImpl::RouteEntryImpl::connect_config_nullopt_;
const std::list<LowerCaseString> AsyncStreamImpl::NullConfig::internal_only_headers_;

AsyncClientImpl::AsyncClientImpl(Upstream::ClusterInfoConstSharedPtr cluster,
                                 Stats::Store& stats_store, Event::Dispatcher& dispatcher,
                                 const LocalInfo::LocalInfo& local_info,
                                 Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                                 Random::RandomGenerator& random,
                                 Router::ShadowWriterPtr&& shadow_writer,
                                 Http::Context& http_context)
    : cluster_(cluster), config_("http.async-client.", local_info, stats_store, cm, runtime, random,
                                 std::move(shadow_writer), true, false, false, false, {},
                                 dispatcher.timeSource(), http_context),
      dispatcher_(dispatcher) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->reset();
  }
}

AsyncClient::Request* AsyncClientImpl::send(RequestMessagePtr&& request,
                                            AsyncClient::Callbacks& callbacks,
                                            const AsyncClient::RequestOptions& options) {
  AsyncRequestImpl* async_request =
      new AsyncRequestImpl(std::move(request), *this, callbacks, options);
  async_request->initialize();
  std::unique_ptr<AsyncStreamImpl> new_request{async_request};

  // The request may get immediately failed. If so, we will return nullptr.
  if (!new_request->remote_closed_) {
    new_request->moveIntoList(std::move(new_request), active_streams_);
    return async_request;
  } else {
    new_request->cleanup();
    return nullptr;
  }
}

AsyncClient::Stream* AsyncClientImpl::start(AsyncClient::StreamCallbacks& callbacks,
                                            const AsyncClient::StreamOptions& options) {
  std::unique_ptr<AsyncStreamImpl> new_stream{new AsyncStreamImpl(*this, callbacks, options)};
  new_stream->moveIntoList(std::move(new_stream), active_streams_);
  return active_streams_.front().get();
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                                 const AsyncClient::StreamOptions& options)
    : parent_(parent), stream_callbacks_(callbacks), stream_id_(parent.config_.random_.random()),
      router_(parent.config_), stream_info_(Protocol::Http11, parent.dispatcher().timeSource()),
      tracing_config_(Tracing::EgressConfig::get()),
      route_(std::make_shared<RouteImpl>(parent_.cluster_->name(), options.timeout,
                                         options.hash_policy)),
      send_xff_(options.send_xff) {
  if (options.buffer_body_for_retry) {
    buffered_body_ = std::make_unique<Buffer::OwnedImpl>();
  }

  router_.setDecoderFilterCallbacks(*this);
  // TODO(mattklein123): Correctly set protocol in stream info when we support access logging.
}

void AsyncStreamImpl::encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
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
  // Map send calls after local closure to no-ops. The send call could have been queued prior to
  // remote reset or closure, and/or closure could have occurred synchronously in response to a
  // previous send. In these cases the router will have already cleaned up stream state. This
  // parallels handling in the main Http::ConnectionManagerImpl as well.
  if (local_closed_) {
    return;
  }

  // TODO(mattklein123): We trust callers currently to not do anything insane here if they set up
  // buffering on an async client call. We should potentially think about limiting the size of
  // buffering that we allow here.
  if (buffered_body_ != nullptr) {
    buffered_body_->add(data);
  }

  router_.decodeData(data, end_stream);
  closeLocal(end_stream);
}

void AsyncStreamImpl::sendTrailers(RequestTrailerMap& trailers) {
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
  local_closed_ = remote_closed_ = true;
  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (inserted()) {
    dispatcher().deferredDelete(removeFromList(parent_.active_streams_));
  }
}

void AsyncStreamImpl::resetStream() {
  stream_callbacks_.onReset();
  cleanup();
}

AsyncRequestImpl::AsyncRequestImpl(RequestMessagePtr&& request, AsyncClientImpl& parent,
                                   AsyncClient::Callbacks& callbacks,
                                   const AsyncClient::RequestOptions& options)
    : AsyncStreamImpl(parent, *this, options), request_(std::move(request)), callbacks_(callbacks) {
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
  child_span_->setSampled(options.sampled_);
}

void AsyncRequestImpl::initialize() {
  child_span_->injectContext(request_->headers());
  sendHeaders(request_->headers(), !request_->body());
  if (request_->body()) {
    // It's possible this will be a no-op due to a local response synchronously generated in
    // sendHeaders; guards handle this within AsyncStreamImpl.
    sendData(*request_->body(), true);
  }
  // TODO(mattklein123): Support request trailers.
}

void AsyncRequestImpl::onComplete() {
  callbacks_.onBeforeFinalizeUpstreamSpan(*child_span_, &response_->headers());

  Tracing::HttpTracerUtility::finalizeUpstreamSpan(*child_span_, &response_->headers(),
                                                   response_->trailers(), streamInfo(),
                                                   Tracing::EgressConfig::get());

  callbacks_.onSuccess(*this, std::move(response_));
}

void AsyncRequestImpl::onHeaders(ResponseHeaderMapPtr&& headers, bool) {
  const uint64_t response_code = Http::Utility::getResponseStatus(*headers);
  streamInfo().response_code_ = response_code;
  response_ = std::make_unique<ResponseMessageImpl>(std::move(headers));
}

void AsyncRequestImpl::onData(Buffer::Instance& data, bool) {
  if (!response_->body()) {
    response_->body() = std::make_unique<Buffer::OwnedImpl>();
  }
  streamInfo().addBytesReceived(data.length());
  response_->body()->move(data);
}

void AsyncRequestImpl::onTrailers(ResponseTrailerMapPtr&& trailers) {
  response_->trailers(std::move(trailers));
}

void AsyncRequestImpl::onReset() {
  if (!cancelled_) {
    // Set "error reason" tag related to reset. The tagging for "error true" is done inside the
    // Tracing::HttpTracerUtility::finalizeUpstreamSpan.
    child_span_->setTag(Tracing::Tags::get().ErrorReason, "Reset");
  }

  callbacks_.onBeforeFinalizeUpstreamSpan(*child_span_,
                                          remoteClosed() ? &response_->headers() : nullptr);

  // Finalize the span based on whether we received a response or not.
  Tracing::HttpTracerUtility::finalizeUpstreamSpan(
      *child_span_, remoteClosed() ? &response_->headers() : nullptr,
      remoteClosed() ? response_->trailers() : nullptr, streamInfo(), Tracing::EgressConfig::get());

  if (!cancelled_) {
    // In this case we don't have a valid response so we do need to raise a failure.
    callbacks_.onFailure(*this, AsyncClient::FailureReason::Reset);
  }
}

void AsyncRequestImpl::cancel() {
  cancelled_ = true;

  // Add tags about the cancellation.
  child_span_->setTag(Tracing::Tags::get().Canceled, Tracing::Tags::get().True);

  reset();
}

} // namespace Http
} // namespace Envoy
