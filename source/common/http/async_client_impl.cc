#include "async_client_impl.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/headers.h"

namespace Http {

const Http::HeaderMapImpl AsyncRequestImpl::SERVICE_UNAVAILABLE_HEADER{
    {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::ServiceUnavailable))}};

const Http::HeaderMapImpl AsyncRequestImpl::REQUEST_TIMEOUT_HEADER{
    {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::GatewayTimeout))}};

AsyncClientImpl::AsyncClientImpl(ConnectionPool::Instance& conn_pool, const std::string& cluster,
                                 Stats::Store& stats_store, Event::Dispatcher& dispatcher)
    : conn_pool_(conn_pool), stat_prefix_(fmt::format("cluster.{}.", cluster)),
      stats_store_(stats_store), dispatcher_(dispatcher) {}

AsyncClient::RequestPtr AsyncClientImpl::send(MessagePtr&& request,
                                              AsyncClient::Callbacks& callbacks,
                                              const Optional<std::chrono::milliseconds>& timeout) {
  std::unique_ptr<AsyncRequestImpl> new_request{
      new AsyncRequestImpl(std::move(request), *this, callbacks, dispatcher_, timeout)};

  // The request may get immediately failed. If so, we will return nullptr.
  if (new_request->stream_encoder_) {
    return std::move(new_request);
  } else {
    return nullptr;
  }
}

AsyncRequestImpl::AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent,
                                   AsyncClient::Callbacks& callbacks, Event::Dispatcher& dispatcher,
                                   const Optional<std::chrono::milliseconds>& timeout)
    : request_(std::move(request)), parent_(parent), callbacks_(callbacks) {

  stream_encoder_.reset(new PooledStreamEncoder(parent_.conn_pool_, *this, *this, 0, 0, *this));
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

  Http::CodeUtility::ResponseStatInfo info{parent_.stats_store_, parent_.stat_prefix_,
                                           response_->headers(), true, EMPTY_STRING, EMPTY_STRING};
  Http::CodeUtility::chargeResponseStat(info);

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
  // TODO: Check host's canary status in addition to canary header.
  Http::CodeUtility::ResponseTimingInfo info{
      parent_.stats_store_, parent_.stat_prefix_, stream_encoder_->requestCompleteTime(),
      response_->headers().get(Http::Headers::get().EnvoyUpstreamCanary) == "true", true,
      EMPTY_STRING, EMPTY_STRING};
  Http::CodeUtility::chargeResponseTiming(info);

  cleanup();
  callbacks_.onSuccess(std::move(response_));
}

void AsyncRequestImpl::onResetStream(StreamResetReason) {
  Http::CodeUtility::ResponseStatInfo info{parent_.stats_store_, parent_.stat_prefix_,
                                           SERVICE_UNAVAILABLE_HEADER, true, EMPTY_STRING,
                                           EMPTY_STRING};
  Http::CodeUtility::chargeResponseStat(info);
  cleanup();
  callbacks_.onFailure(Http::AsyncClient::FailureReason::Reset);
}

void AsyncRequestImpl::onRequestTimeout() {
  Http::CodeUtility::ResponseStatInfo info{parent_.stats_store_, parent_.stat_prefix_,
                                           REQUEST_TIMEOUT_HEADER, true, EMPTY_STRING,
                                           EMPTY_STRING};
  Http::CodeUtility::chargeResponseStat(info);
  parent_.stats_store_.counter(fmt::format("{}upstream_rq_timeout", parent_.stat_prefix_)).inc();
  stream_encoder_->resetStream();
  cleanup();
  callbacks_.onFailure(Http::AsyncClient::FailureReason::RequestTimemout);
}

void AsyncRequestImpl::cleanup() {
  stream_encoder_.reset();
  if (request_timeout_) {
    request_timeout_->disableTimer();
  }
}
} // Http
