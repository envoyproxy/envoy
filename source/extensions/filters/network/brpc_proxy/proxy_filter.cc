#include "extensions/filters/network/brpc_proxy/proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

BrpcProxyStatsSharedPtr generateStats(const std::string& prefix, Stats::Scope& scope) {
  return std::shared_ptr<BrpcProxyStats> { new BrpcProxyStats{
	  ALL_BRPC_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix), POOL_GAUGE_PREFIX(scope, prefix))}};
}


ProxyFilter::ProxyFilter(DecoderFactory& factory,
                         EncoderPtr&& encoder, RequestManager::RMInstance& manager,
                         BrpcProxyStatsSharedPtr stat)
    : decoder_(factory.create(*this)), encoder_(std::move(encoder)), manager_(manager)
  	, stat_(stat) {
}

ProxyFilter::~ProxyFilter() {
  ASSERT(pending_requests_.empty()); 
}

void ProxyFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  callbacks_->connection().addConnectionCallbacks(*this);
  callbacks_->connection().setConnectionStats({stat_->downstream_cx_rx_bytes_total_,
                                               stat_->downstream_cx_rx_bytes_buffered_,
                                               stat_->downstream_cx_tx_bytes_total_,
                                               stat_->downstream_cx_tx_bytes_buffered_,
                                               nullptr, nullptr});
}

void ProxyFilter::onMessage(BrpcMessagePtr&& value) {
  pending_requests_.emplace_back(*this, value->request_id());
  PendingRequest& request = pending_requests_.back();
  BrpcRequestPtr requester =
      manager_.makeRequest(std::move(value), request, callbacks_->connection().dispatcher());
  if (requester) {
    // The requester can immediately respond and destroy the pending request. Only store the handle
    // if the request is still alive.
    request.request_handle_ = std::move(requester);
  }
}

void ProxyFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    while (!pending_requests_.empty()) {
      if (pending_requests_.front().request_handle_ != nullptr) {
        pending_requests_.front().request_handle_->cancel();
      }
      pending_requests_.pop_front();
    }
  }
}

void ProxyFilter::onResponse(PendingRequest& request, BrpcMessagePtr&& value) {
  ASSERT(!pending_requests_.empty());
  request.pending_response_ = std::move(value);
  request.request_handle_ = nullptr;
  //todo use map because brpc support unorder response
  while (!pending_requests_.empty() && pending_requests_.front().pending_response_) {
    encoder_->encode(*pending_requests_.front().pending_response_, encoder_buffer_);
    pending_requests_.pop_front();
  }

  if (encoder_buffer_.length() > 0) {
    callbacks_->connection().write(encoder_buffer_, false);
  }

  // Check for drain close only if there are no pending responses.
  /*
  if (pending_requests_.empty() && config_->drain_decision_.drainClose() &&
      config_->runtime_.snapshot().featureEnabled(config_->redis_drain_close_runtime_key_, 100)) {
    config_->stats_.downstream_cx_drain_close_.inc();
    callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }*/
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data, bool) {
  try {
    decoder_->decode(data);
    return Network::FilterStatus::Continue;
  } catch (ProtocolError&) {
    stat_->downstream_cx_protocol_error_.inc();
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
}

ProxyFilter::PendingRequest::PendingRequest(ProxyFilter& parent, int64_t request_id) : parent_(parent)
	, request_id_(request_id) {
  parent.stat_->downstream_rq_total_.inc();
  parent.stat_->downstream_rq_active_.inc();
}

ProxyFilter::PendingRequest::~PendingRequest() {
  parent_.stat_->downstream_rq_active_.dec();
}

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

