#include "async_client_impl.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/headers.h"

namespace Http {

const HeaderMapImpl AsyncRequestImpl::SERVICE_UNAVAILABLE_HEADER{
    {Headers::get().Status, std::to_string(enumToInt(Code::ServiceUnavailable))}};

const HeaderMapImpl AsyncRequestImpl::REQUEST_TIMEOUT_HEADER{
    {Headers::get().Status, std::to_string(enumToInt(Code::GatewayTimeout))}};

AsyncClientImpl::AsyncClientImpl(const Upstream::Cluster& cluster,
                                 AsyncClientConnPoolFactory& factory, Stats::Store& stats_store,
                                 Event::Dispatcher& dispatcher, const std::string& local_zone_name)
    : cluster_(cluster), factory_(factory), stats_store_(stats_store), dispatcher_(dispatcher),
      local_zone_name_(local_zone_name), stat_prefix_(fmt::format("cluster.{}.", cluster.name())) {}

AsyncClientImpl::~AsyncClientImpl() { ASSERT(active_requests_.empty()); }

AsyncClient::Request* AsyncClientImpl::send(MessagePtr&& request, AsyncClient::Callbacks& callbacks,
                                            const Optional<std::chrono::milliseconds>& timeout) {
  ConnectionPool::Instance* conn_pool = factory_.connPool();
  if (!conn_pool) {
    callbacks.onFailure(AsyncClient::FailureReason::Reset);
    return nullptr;
  }

  std::unique_ptr<AsyncRequestImpl> new_request{
      new AsyncRequestImpl(std::move(request), *this, callbacks, dispatcher_, *conn_pool, timeout)};

  // The request may get immediately failed. If so, we will return nullptr.
  if (new_request->stream_encoder_) {
    new_request->moveIntoList(std::move(new_request), active_requests_);
    return active_requests_.front().get();
  } else {
    return nullptr;
  }
}

AsyncRequestImpl::AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent,
                                   AsyncClient::Callbacks& callbacks, Event::Dispatcher& dispatcher,
                                   ConnectionPool::Instance& conn_pool,
                                   const Optional<std::chrono::milliseconds>& timeout)
    : request_(std::move(request)), parent_(parent), callbacks_(callbacks) {

  stream_encoder_.reset(new PooledStreamEncoder(conn_pool, *this, *this, 0, 0, *this));
  stream_encoder_->encodeHeaders(request_->headers(), !request_->body());

  // We might have been immediately failed.
  if (stream_encoder_ && request_->body()) {
    stream_encoder_->encodeData(*request_->body(), true);
  }
  if (stream_encoder_ && timeout.valid()) {
    request_timeout_ = dispatcher.createTimer([this]() -> void { onRequestTimeout(); });
    request_timeout_->enableTimer(timeout.value());
  }
}

AsyncRequestImpl::~AsyncRequestImpl() { ASSERT(!stream_encoder_); }

const std::string& AsyncRequestImpl::upstreamZone() {
  return upstream_host_ ? upstream_host_->zone() : EMPTY_STRING;
}

void AsyncRequestImpl::cancel() {
  ASSERT(stream_encoder_);
  stream_encoder_->resetStream();
  cleanup();
}

void AsyncRequestImpl::decodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
  response_.reset(new ResponseMessageImpl(std::move(headers)));
#ifndef NDEBUG
  log_debug("async http request response headers (end_stream={}):", end_stream);
  response_->headers().iterate([](const LowerCaseString& key, const std::string& value)
                                   -> void { log_debug("  '{}':'{}'", key.get(), value); });
#endif

  CodeUtility::ResponseStatInfo info{parent_.stats_store_, parent_.stat_prefix_,
                                     response_->headers(), true, EMPTY_STRING, EMPTY_STRING,
                                     parent_.local_zone_name_, upstreamZone()};
  CodeUtility::chargeResponseStat(info);

  if (end_stream) {
    onComplete();
  }
}

void AsyncRequestImpl::decodeData(const Buffer::Instance& data, bool end_stream) {
  log_trace("async http request response data (length={} end_stream={})", data.length(),
            end_stream);
  if (!response_->body()) {
    response_->body(Buffer::InstancePtr{new Buffer::OwnedImpl(data)});
  } else {
    response_->body()->add(data);
  }

  if (end_stream) {
    onComplete();
  }
}

void AsyncRequestImpl::decodeTrailers(HeaderMapPtr&& trailers) {
  response_->trailers(std::move(trailers));
#ifndef NDEBUG
  log_debug("async http request response trailers:");
  response_->trailers()->iterate([](const LowerCaseString& key, const std::string& value)
                                     -> void { log_debug("  '{}':'{}'", key.get(), value); });
#endif

  onComplete();
}

void AsyncRequestImpl::onComplete() {
  CodeUtility::ResponseTimingInfo info{
      parent_.stats_store_, parent_.stat_prefix_, stream_encoder_->requestCompleteTime(),
      response_->headers().get(Headers::get().EnvoyUpstreamCanary) == "true" ||
      upstream_host_ ? upstream_host_->canary() : false,
      true, EMPTY_STRING, EMPTY_STRING, parent_.local_zone_name_, upstreamZone()};
  CodeUtility::chargeResponseTiming(info);

  callbacks_.onSuccess(std::move(response_));
  cleanup();
}

void AsyncRequestImpl::onResetStream(StreamResetReason) {
  CodeUtility::ResponseStatInfo info{parent_.stats_store_, parent_.stat_prefix_,
                                     SERVICE_UNAVAILABLE_HEADER, true, EMPTY_STRING, EMPTY_STRING,
                                     parent_.local_zone_name_, upstreamZone()};
  CodeUtility::chargeResponseStat(info);
  callbacks_.onFailure(AsyncClient::FailureReason::Reset);
  cleanup();
}

void AsyncRequestImpl::onRequestTimeout() {
  CodeUtility::ResponseStatInfo info{parent_.stats_store_, parent_.stat_prefix_,
                                     REQUEST_TIMEOUT_HEADER, true, EMPTY_STRING, EMPTY_STRING,
                                     parent_.local_zone_name_, upstreamZone()};
  CodeUtility::chargeResponseStat(info);
  parent_.cluster_.stats().upstream_rq_timeout_.inc();
  stream_encoder_->resetStream();
  callbacks_.onFailure(AsyncClient::FailureReason::RequestTimeout);
  cleanup();
}

void AsyncRequestImpl::cleanup() {
  stream_encoder_.reset();
  if (request_timeout_) {
    request_timeout_->disableTimer();
  }

  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (inserted()) {
    removeFromList(parent_.active_requests_);
  }
}

} // Http
