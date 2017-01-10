#pragma once

#include "envoy/network/filter.h"
#include "envoy/redis/codec.h"
#include "envoy/redis/conn_pool.h"

#include "common/buffer/buffer_impl.h"
#include "common/json/json_loader.h"

namespace Redis {

// TODO: Stats
// TODO: Actual multiplexing, command verification, and splitting

/**
 * Configuration for the redis proxy filter.
 */
class ProxyFilterConfig {
public:
  ProxyFilterConfig(const Json::Object& config, Upstream::ClusterManager& cm);

  const std::string& clusterName() { return cluster_name_; }

private:
  const std::string cluster_name_;
};

/**
 * A redis multiplexing proxy filter. This filter will take incoming redis pipelined commands, and
 * mulitplex them onto a consistently hashed connection pool of backend servers.
 * TODO: When we actually support command splitting, better documentation of what we support and
 *       what we are doing.
 */
class ProxyFilter : public Network::ReadFilter,
                    public DecoderCallbacks,
                    public Network::ConnectionCallbacks {
public:
  ProxyFilter(DecoderFactory& factory, EncoderPtr&& encoder, ConnPool::Instance& conn_pool)
      : decoder_(factory.create(*this)), encoder_(std::move(encoder)), conn_pool_(conn_pool) {}

  ~ProxyFilter();

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    callbacks_->connection().addConnectionCallbacks(*this);
  }
  Network::FilterStatus onData(Buffer::Instance& data) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  // Network::ConnectionCallbacks
  void onEvent(uint32_t events) override;

  // Redis::DecoderCallbacks
  void onRespValue(RespValuePtr&& value) override;

private:
  struct PendingRequest : public ConnPool::ActiveRequestCallbacks {
    PendingRequest(ProxyFilter& parent) : parent_(parent) {}

    // Redis::ConnPool::ActiveRequestCallbacks
    void onResponse(RespValuePtr&& value) override { parent_.onResponse(*this, std::move(value)); }
    void onFailure() override { parent_.onFailure(*this); }

    ProxyFilter& parent_;
    ConnPool::ActiveRequest* request_handle_;
  };

  void onResponse(PendingRequest& request, RespValuePtr&& value);
  void onFailure(PendingRequest& request);
  void respondWithFailure(const std::string& message);

  DecoderPtr decoder_;
  EncoderPtr encoder_;
  ConnPool::Instance& conn_pool_;
  Buffer::OwnedImpl encoder_buffer_;
  Network::ReadFilterCallbacks* callbacks_{};
  std::list<PendingRequest> pending_requests_;
};

} // Redis
