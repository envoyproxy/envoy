#include "proxy_filter.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"

namespace Redis {

ProxyFilterConfig::ProxyFilterConfig(const Json::Object& config, Upstream::ClusterManager& cm)
    : cluster_name_{config.getString("cluster_name")} {

  config.validateSchema(Json::Schema::REDIS_PROXY_NETWORK_FILTER_SCHEMA);

  if (!cm.get(cluster_name_)) {
    throw EnvoyException(
        fmt::format("redis filter config: unknown cluster name '{}'", cluster_name_));
  }
}

ProxyFilter::~ProxyFilter() { ASSERT(pending_requests_.empty()); }

void ProxyFilter::onRespValue(RespValuePtr&& value) {
  pending_requests_.emplace_back(*this);
  PendingRequest& request = pending_requests_.back();
  request.request_handle_ = conn_pool_.makeRequest("", *value, request);
  if (!request.request_handle_) {
    respondWithFailure("no healthy upstream");
    pending_requests_.pop_back();
  }
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
  // TODO: Currently the connection pool is a single connection so out of order can't happen.
  ASSERT(!pending_requests_.empty());
  ASSERT(&request == &pending_requests_.front());
  UNREFERENCED_PARAMETER(request);
  pending_requests_.pop_front();
  encoder_->encode(*value, encoder_buffer_);
  callbacks_->connection().write(encoder_buffer_);
}

void ProxyFilter::onFailure(PendingRequest& request) {
  // TODO: Currently the connection pool is a single connection so out of order can't happen.
  ASSERT(!pending_requests_.empty());
  ASSERT(&request == &pending_requests_.front());
  UNREFERENCED_PARAMETER(request);
  pending_requests_.pop_front();
  respondWithFailure("upstream connection error");
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data) {
  try {
    decoder_->decode(data);
    return Network::FilterStatus::Continue;
  } catch (ProtocolError&) {
    respondWithFailure("downstream protocol error");
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
}

void ProxyFilter::respondWithFailure(const std::string& message) {
  RespValue error;
  error.type(RespType::Error);
  error.asString() = message;
  encoder_->encode(error, encoder_buffer_);
  callbacks_->connection().write(encoder_buffer_);
}

} // Redis
