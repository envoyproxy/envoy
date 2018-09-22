#include "common/http/async_client_impl.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/grpc/common.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

const std::list<std::string> AsyncStreamImpl::NullCorsPolicy::allow_origin_;
const std::list<std::regex> AsyncStreamImpl::NullCorsPolicy::allow_origin_regex_;
const absl::optional<bool> AsyncStreamImpl::NullCorsPolicy::allow_credentials_;
const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
    AsyncStreamImpl::NullRateLimitPolicy::rate_limit_policy_entry_;
const AsyncStreamImpl::NullRateLimitPolicy AsyncStreamImpl::RouteEntryImpl::rate_limit_policy_;
const AsyncStreamImpl::NullRetryPolicy AsyncStreamImpl::RouteEntryImpl::retry_policy_;
const AsyncStreamImpl::NullShadowPolicy AsyncStreamImpl::RouteEntryImpl::shadow_policy_;
const AsyncStreamImpl::NullVirtualHost AsyncStreamImpl::RouteEntryImpl::virtual_host_;
const AsyncStreamImpl::NullRateLimitPolicy AsyncStreamImpl::NullVirtualHost::rate_limit_policy_;
const AsyncStreamImpl::NullConfig AsyncStreamImpl::NullVirtualHost::route_configuration_;
const std::multimap<std::string, std::string> AsyncStreamImpl::RouteEntryImpl::opaque_config_;
const envoy::api::v2::core::Metadata AsyncStreamImpl::RouteEntryImpl::metadata_;
const AsyncStreamImpl::NullPathMatchCriterion
    AsyncStreamImpl::RouteEntryImpl::path_match_criterion_;
const std::list<LowerCaseString> AsyncStreamImpl::NullConfig::internal_only_headers_;

AsyncClientImpl::AsyncClientImpl(const Upstream::ClusterInfo& cluster, Stats::Store& stats_store,
                                 Event::Dispatcher& dispatcher,
                                 const LocalInfo::LocalInfo& local_info,
                                 Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                                 Runtime::RandomGenerator& random,
                                 Router::ShadowWriterPtr&& shadow_writer)
    : cluster_(cluster),
      config_("http.async-client.", local_info, stats_store, cm, runtime, random,
              std::move(shadow_writer), true, false, false, dispatcher.timeSystem()),
      dispatcher_(dispatcher) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_streams_.empty()) {
    active_streams_.front()->reset();
  }
}

AsyncClient::Request*
AsyncClientImpl::send(MessagePtr&& request, AsyncClient::Callbacks& callbacks,
                      const absl::optional<std::chrono::milliseconds>& timeout) {
  AsyncRequestImpl* async_request =
      new AsyncRequestImpl(std::move(request), *this, callbacks, timeout);
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

AsyncClient::Stream*
AsyncClientImpl::start(AsyncClient::StreamCallbacks& callbacks,
                       const absl::optional<std::chrono::milliseconds>& timeout,
                       bool buffer_body_for_retry) {
  std::unique_ptr<AsyncStreamImpl> new_stream{
      new AsyncStreamImpl(*this, callbacks, timeout, buffer_body_for_retry)};
  new_stream->moveIntoList(std::move(new_stream), active_streams_);
  return active_streams_.front().get();
}

AsyncStreamImpl::AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                                 const absl::optional<std::chrono::milliseconds>& timeout,
                                 bool buffer_body_for_retry)
    : parent_(parent), stream_callbacks_(callbacks), stream_id_(parent.config_.random_.random()),
      router_(parent.config_), stream_info_(Protocol::Http11, parent.dispatcher().timeSystem()),
      tracing_config_(Tracing::EgressConfig::get()),
      route_(std::make_shared<RouteImpl>(parent_.cluster_.name(), timeout)) {
  if (buffer_body_for_retry) {
    buffered_body_.reset(new Buffer::OwnedImpl());
  }

  router_.setDecoderFilterCallbacks(*this);
  // TODO(mattklein123): Correctly set protocol in request info when we support access logging.
}

void AsyncStreamImpl::encodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
  ENVOY_LOG(debug, "async http request response headers (end_stream={}):\n{}", end_stream,
            *headers);
  ASSERT(!remote_closed_);
  stream_callbacks_.onHeaders(std::move(headers), end_stream);
  closeRemote(end_stream);
}

void AsyncStreamImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "async http request response data (length={} end_stream={})", data.length(),
            end_stream);
  ASSERT(!remote_closed_);
  stream_callbacks_.onData(data, end_stream);
  closeRemote(end_stream);
}

void AsyncStreamImpl::encodeTrailers(HeaderMapPtr&& trailers) {
  ENVOY_LOG(debug, "async http request response trailers:\n{}", *trailers);
  ASSERT(!remote_closed_);
  stream_callbacks_.onTrailers(std::move(trailers));
  closeRemote(true);
}

void AsyncStreamImpl::sendHeaders(HeaderMap& headers, bool end_stream) {
  if (Http::Headers::get().MethodValues.Head == headers.Method()->value().c_str()) {
    is_head_request_ = true;
  }

  is_grpc_request_ = Grpc::Common::hasGrpcContentType(headers);
  headers.insertEnvoyInternalRequest().value().setReference(
      Headers::get().EnvoyInternalRequestValues.True);
  Utility::appendXff(headers, *parent_.config_.local_info_.address());
  router_.decodeHeaders(headers, end_stream);
  closeLocal(end_stream);
}

void AsyncStreamImpl::sendData(Buffer::Instance& data, bool end_stream) {
  // TODO(mattklein123): We trust callers currently to not do anything insane here if they set up
  // buffering on an async client call. We should potentially think about limiting the size of
  // buffering that we allow here.
  if (buffered_body_ != nullptr) {
    buffered_body_->add(data);
  }

  router_.decodeData(data, end_stream);
  closeLocal(end_stream);
}

void AsyncStreamImpl::sendTrailers(HeaderMap& trailers) {
  router_.decodeTrailers(trailers);
  closeLocal(true);
}

void AsyncStreamImpl::closeLocal(bool end_stream) {
  ASSERT(!(local_closed_ && end_stream));

  local_closed_ |= end_stream;
  if (complete()) {
    cleanup();
  }
}

void AsyncStreamImpl::closeRemote(bool end_stream) {
  remote_closed_ |= end_stream;
  if (complete()) {
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

AsyncRequestImpl::AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent,
                                   AsyncClient::Callbacks& callbacks,
                                   const absl::optional<std::chrono::milliseconds>& timeout)
    // We tell the underlying stream to not buffer because we already have the full request and
    // and can handle any buffered body requests.
    : AsyncStreamImpl(parent, *this, timeout, false), request_(std::move(request)),
      callbacks_(callbacks) {}

void AsyncRequestImpl::initialize() {
  sendHeaders(request_->headers(), !request_->body());
  if (!remoteClosed() && request_->body()) {
    sendData(*request_->body(), true);
  }
  // TODO(mattklein123): Support request trailers.
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
    response_->body().reset(new Buffer::OwnedImpl());
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

void AsyncRequestImpl::onReset() {
  if (!cancelled_) {
    // In this case we don't have a valid response so we do need to raise a failure.
    callbacks_.onFailure(AsyncClient::FailureReason::Reset);
  }
}

void AsyncRequestImpl::cancel() {
  cancelled_ = true;
  reset();
}

} // namespace Http
} // namespace Envoy
