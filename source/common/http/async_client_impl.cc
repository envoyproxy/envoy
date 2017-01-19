#include "async_client_impl.h"

namespace Http {

const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
    AsyncStreamImpl::NullRateLimitPolicy::rate_limit_policy_entry_;
const AsyncStreamImpl::NullRateLimitPolicy AsyncStreamImpl::RouteEntryImpl::rate_limit_policy_;
const AsyncStreamImpl::NullRetryPolicy AsyncStreamImpl::RouteEntryImpl::retry_policy_;
const AsyncStreamImpl::NullShadowPolicy AsyncStreamImpl::RouteEntryImpl::shadow_policy_;
const AsyncStreamImpl::NullVirtualHost AsyncStreamImpl::RouteEntryImpl::virtual_host_;
const AsyncStreamImpl::NullRateLimitPolicy AsyncStreamImpl::NullVirtualHost::rate_limit_policy_;

AsyncClientImpl::AsyncClientImpl(const Upstream::ClusterInfo& cluster, Stats::Store& stats_store,
                                 Event::Dispatcher& dispatcher,
                                 const LocalInfo::LocalInfo& local_info,
                                 Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                                 Runtime::RandomGenerator& random,
                                 Router::ShadowWriterPtr&& shadow_writer)
    : cluster_(cluster), config_("http.async-client.", local_info, stats_store, cm, runtime, random,
                                 std::move(shadow_writer), true),
      dispatcher_(dispatcher) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->failDueToClientDestroy();
  }
}

AsyncClient::Request* AsyncClientImpl::send(MessagePtr&& request, AsyncClient::Callbacks& callbacks,
                                            const Optional<std::chrono::milliseconds>& timeout) {
  AsyncRequestImpl* async_request =
      new AsyncRequestImpl(std::move(request), *this, callbacks, timeout);
  std::unique_ptr<AsyncStreamImpl> new_request{async_request};

  // The request may get immediately failed. If so, we will return nullptr.
  if (!new_request->complete()) {
    new_request->moveIntoList(std::move(new_request), active_streams_);
    return async_request;
  } else {
    return nullptr;
  }
}

AsyncClient::Stream* AsyncClientImpl::start(AsyncClient::StreamCallbacks& callbacks,
                                            const Optional<std::chrono::milliseconds>& timeout) {
  std::unique_ptr<AsyncStreamImpl> new_stream{new AsyncStreamImpl(*this, callbacks, timeout)};

  // The request may get immediately failed. If so, we will return nullptr.
  if (!new_stream->complete()) {
    new_stream->moveIntoList(std::move(new_stream), active_streams_);
    return active_streams_.front().get();
  } else {
    return nullptr;
  }
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                                 const Optional<std::chrono::milliseconds>& timeout)
    : parent_(parent), stream_callbacks_(callbacks), stream_id_(parent.config_.random_.random()),
      router_(parent.config_), request_info_(Protocol::Http11),
      route_(parent_.cluster_.name(), timeout) {

  router_.setDecoderFilterCallbacks(*this);
  // TODO: Correctly set protocol in request info when we support access logging.
}

AsyncStreamImpl::~AsyncStreamImpl() { ASSERT(!reset_callback_); }

void AsyncStreamImpl::encodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
#ifndef NDEBUG
  log_debug("async http request response headers (end_stream={}):", end_stream);
  headers->iterate([](const HeaderEntry& header, void*) -> void {
    log_debug("  '{}':'{}'", header.key().c_str(), header.value().c_str());
  }, nullptr);
#endif

  stream_callbacks_.onHeaders(std::move(headers), end_stream);
  closeRemote(end_stream);
}

void AsyncStreamImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  log_trace("async http request response data (length={} end_stream={})", data.length(),
            end_stream);
  stream_callbacks_.onData(data, end_stream);
  closeRemote(end_stream);
}

void AsyncStreamImpl::encodeTrailers(HeaderMapPtr&& trailers) {
#ifndef NDEBUG
  log_debug("async http request response trailers:");
  trailers->iterate([](const HeaderEntry& header, void*) -> void {
    log_debug("  '{}':'{}'", header.key().c_str(), header.value().c_str());
  }, nullptr);
#endif

  stream_callbacks_.onTrailers(std::move(trailers));
  closeRemote(true);
}

void AsyncStreamImpl::sendHeaders(HeaderMap& headers, bool end_stream) {
  headers.insertEnvoyInternalRequest().value(Headers::get().EnvoyInternalRequestValues.True);
  headers.insertForwardedFor().value(parent_.config_.local_info_.address());
  router_.decodeHeaders(headers, end_stream);
  closeLocal(end_stream);
}

void AsyncStreamImpl::sendData(Buffer::Instance& data, bool end_stream) {
  router_.decodeData(data, end_stream);
  decoding_buffer_ = &data;
  closeLocal(end_stream);
}

void AsyncStreamImpl::sendTrailers(HeaderMap& trailers) {
  router_.decodeTrailers(trailers);
  closeLocal(true);
}

void AsyncStreamImpl::closeLocal(bool end_stream) {
  local_closed_ |= end_stream;
  if (complete())
    cleanup();
}

void AsyncStreamImpl::closeRemote(bool end_stream) {
  remote_closed_ |= end_stream;
  if (complete())
    cleanup();
}

void AsyncStreamImpl::close() {
  reset_callback_();
  cleanup();
}

void AsyncStreamImpl::cleanup() {
  reset_callback_ = nullptr;

  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (inserted()) {
    removeFromList(parent_.active_streams_);
  }
}

void AsyncStreamImpl::resetStream() {
  stream_callbacks_.onResetStream();
  cleanup();
}

void AsyncStreamImpl::failDueToClientDestroy() {
  // In this case we are going away because the client is being destroyed. We need to both reset
  // the stream as well as raise a failure callback.
  reset_callback_();
  resetStream();
}

AsyncRequestImpl::AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent,
                                   AsyncClient::Callbacks& callbacks,
                                   const Optional<std::chrono::milliseconds>& timeout)
    : AsyncStreamImpl(parent, *this, timeout), request_(std::move(request)), callbacks_(callbacks) {

  sendHeaders(request_->headers(), !request_->body());
  if (!complete() && request_->body()) {
    sendData(*request_->body(), true);
  }
  // TODO: Support request trailers.
}

void AsyncRequestImpl::onComplete() { callbacks_.onSuccess(std::move(response_)); }

void AsyncRequestImpl::onHeaders(HeaderMapPtr&& headers, bool end_stream) {
  response_.reset(new ResponseMessageImpl(std::move(headers)));

  if (end_stream) {
    onComplete();
  }
}

void AsyncRequestImpl::onData(Buffer::Instance& data, bool end_stream) {
  if (!response_->body()) {
    response_->body(Buffer::InstancePtr{new Buffer::OwnedImpl()});
  }
  response_->body()->move(data);

  if (end_stream) {
    onComplete();
  }
}

void AsyncRequestImpl::onTrailers(HeaderMapPtr&& trailers) {
  response_->trailers(std::move(trailers));
  onComplete();
}

void AsyncRequestImpl::onResetStream() {
  // In this case we don't have a valid response so we do need to raise a failure.
  callbacks_.onFailure(AsyncClient::FailureReason::Reset);
}

void AsyncRequestImpl::cancel() { close(); }

} // Http
