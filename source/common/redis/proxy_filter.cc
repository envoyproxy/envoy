#include "common/redis/proxy_filter.h"

#include <cstdint>
#include <string>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Redis {

ProxyFilterConfig::ProxyFilterConfig(
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy& config,
    Upstream::ClusterManager& cm, Stats::Scope& scope, const Network::DrainDecision& drain_decision,
    Runtime::Loader& runtime)
    : drain_decision_(drain_decision), runtime_(runtime), cluster_name_(config.cluster()),
      stat_prefix_(fmt::format("redis.{}.", config.stat_prefix())),
      stats_(generateStats(stat_prefix_, scope)) {
  Config::Utility::checkCluster("redis", cluster_name_, cm);
}

ProxyStats ProxyFilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  return {
      ALL_REDIS_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix), POOL_GAUGE_PREFIX(scope, prefix))};
}

ProxyFilter::ProxyFilter(DecoderFactory& factory, EncoderPtr&& encoder,
                         CommandSplitter::Instance& splitter, ProxyFilterConfigSharedPtr config)
    : decoder_(factory.create(*this)), encoder_(std::move(encoder)), splitter_(splitter),
      config_(config) {
  config_->stats_.downstream_cx_total_.inc();
  config_->stats_.downstream_cx_active_.inc();
}

ProxyFilter::~ProxyFilter() {
  ASSERT(pending_requests_.empty());
  config_->stats_.downstream_cx_active_.dec();
}

void ProxyFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  callbacks_->connection().addConnectionCallbacks(*this);
  callbacks_->connection().setConnectionStats({config_->stats_.downstream_cx_rx_bytes_total_,
                                               config_->stats_.downstream_cx_rx_bytes_buffered_,
                                               config_->stats_.downstream_cx_tx_bytes_total_,
                                               config_->stats_.downstream_cx_tx_bytes_buffered_,
                                               nullptr});
}

void ProxyFilter::onRespValue(RespValuePtr&& value) {
  pending_requests_.emplace_back(*this);
  PendingRequest& request = pending_requests_.back();
  CommandSplitter::SplitRequestPtr split = splitter_.makeRequest(*value, request);
  if (split) {
    // The splitter can immediately respond and destroy the pending request. Only store the handle
    // if the request is still alive.
    request.request_handle_ = std::move(split);
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

void ProxyFilter::onResponse(PendingRequest& request, RespValuePtr&& value) {
  ASSERT(!pending_requests_.empty());
  request.pending_response_ = std::move(value);
  request.request_handle_ = nullptr;

  // The response we got might not be in order, so flush out what we can. (A new response may
  // unlock several out of order responses).
  while (!pending_requests_.empty() && pending_requests_.front().pending_response_) {
    encoder_->encode(*pending_requests_.front().pending_response_, encoder_buffer_);
    pending_requests_.pop_front();
  }

  if (encoder_buffer_.length() > 0) {
    callbacks_->connection().write(encoder_buffer_);
  }

  // Check for drain close only if there are no pending responses.
  if (pending_requests_.empty() && config_->drain_decision_.drainClose() &&
      config_->runtime_.snapshot().featureEnabled(config_->redis_drain_close_runtime_key_, 100)) {
    config_->stats_.downstream_cx_drain_close_.inc();
    callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data) {
  try {
    decoder_->decode(data);
    return Network::FilterStatus::Continue;
  } catch (ProtocolError&) {
    config_->stats_.downstream_cx_protocol_error_.inc();
    RespValue error;
    error.type(RespType::Error);
    error.asString() = "downstream protocol error";
    encoder_->encode(error, encoder_buffer_);
    callbacks_->connection().write(encoder_buffer_);
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
}

ProxyFilter::PendingRequest::PendingRequest(ProxyFilter& parent) : parent_(parent) {
  parent.config_->stats_.downstream_rq_total_.inc();
  parent.config_->stats_.downstream_rq_active_.inc();
}

ProxyFilter::PendingRequest::~PendingRequest() {
  parent_.config_->stats_.downstream_rq_active_.dec();
}

} // namespace Redis
} // namespace Envoy
