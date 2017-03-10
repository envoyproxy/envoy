#include "proxy_filter.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"

namespace Redis {

ProxyFilterConfig::ProxyFilterConfig(const Json::Object& config, Upstream::ClusterManager& cm)
    : Json::Validator(config, Json::Schema::REDIS_PROXY_NETWORK_FILTER_SCHEMA),
      cluster_name_{config.getString("cluster_name")} {
  if (!cm.get(cluster_name_)) {
    throw EnvoyException(
        fmt::format("redis filter config: unknown cluster name '{}'", cluster_name_));
  }
}

ProxyFilter::~ProxyFilter() { ASSERT(pending_requests_.empty()); }

void ProxyFilter::onRespValue(RespValuePtr&& value) {
  pending_requests_.emplace_back(*this);
  PendingRequest& request = pending_requests_.back();
  request.request_handle_ = splitter_.makeRequest(*value, request);
  // The splitter can immediately respond.
}

void ProxyFilter::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::RemoteClose ||
      events & Network::ConnectionEvent::LocalClose) {
    while (!pending_requests_.empty()) {
      pending_requests_.front().request_handle_->cancel();
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
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data) {
  try {
    decoder_->decode(data);
    return Network::FilterStatus::Continue;
  } catch (ProtocolError&) {
    RespValue error;
    error.type(RespType::Error);
    error.asString() = "downstream protocol error";
    encoder_->encode(error, encoder_buffer_);
    callbacks_->connection().write(encoder_buffer_);
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
}

} // Redis
