#pragma once

#include "envoy/network/filter.h"
#include "envoy/redis/codec.h"
#include "envoy/redis/command_splitter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/json/json_loader.h"
#include "common/json/json_validator.h"

namespace Redis {

// TODO(mattklein123): Stats

/**
 * Configuration for the redis proxy filter.
 */
class ProxyFilterConfig : Json::Validator {
public:
  ProxyFilterConfig(const Json::Object& config, Upstream::ClusterManager& cm);

  const std::string& clusterName() { return cluster_name_; }

private:
  const std::string cluster_name_;
};

/**
 * A redis multiplexing proxy filter. This filter will take incoming redis pipelined commands, and
 * mulitplex them onto a consistently hashed connection pool of backend servers.
 */
class ProxyFilter : public Network::ReadFilter,
                    public DecoderCallbacks,
                    public Network::ConnectionCallbacks {
public:
  ProxyFilter(DecoderFactory& factory, EncoderPtr&& encoder, CommandSplitter::Instance& splitter)
      : decoder_(factory.create(*this)), encoder_(std::move(encoder)), splitter_(splitter) {}

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
  struct PendingRequest : public CommandSplitter::SplitCallbacks {
    PendingRequest(ProxyFilter& parent) : parent_(parent) {}

    // Redis::CommandSplitter::SplitCallbacks
    void onResponse(RespValuePtr&& value) override { parent_.onResponse(*this, std::move(value)); }

    ProxyFilter& parent_;
    RespValuePtr pending_response_;
    CommandSplitter::SplitRequestPtr request_handle_;
  };

  void onResponse(PendingRequest& request, RespValuePtr&& value);

  DecoderPtr decoder_;
  EncoderPtr encoder_;
  CommandSplitter::Instance& splitter_;
  Buffer::OwnedImpl encoder_buffer_;
  Network::ReadFilterCallbacks* callbacks_{};
  std::list<PendingRequest> pending_requests_;
};

} // Redis
