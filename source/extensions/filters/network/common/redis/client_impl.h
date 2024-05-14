#pragma once

#include <chrono>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/stats/timespan.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hash.h"
#include "source/common/network/filter_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

// TODO(mattklein123): Circuit breaking
// TODO(rshriram): Fault injection

struct RedirectionValues {
  const std::string ASK = "ASK";
  const std::string MOVED = "MOVED";
  const std::string CLUSTER_DOWN = "CLUSTERDOWN";
};

using RedirectionResponse = ConstSingleton<RedirectionValues>;

class ConfigImpl : public Config {
public:
  ConfigImpl(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings&
          config);

  bool disableOutlierEvents() const override { return false; }
  std::chrono::milliseconds opTimeout() const override { return op_timeout_; }
  bool enableHashtagging() const override { return enable_hashtagging_; }
  bool enableRedirection() const override { return enable_redirection_; }
  uint32_t maxBufferSizeBeforeFlush() const override { return max_buffer_size_before_flush_; }
  std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
    return buffer_flush_timeout_;
  }
  uint32_t maxUpstreamUnknownConnections() const override {
    return max_upstream_unknown_connections_;
  }
  bool enableCommandStats() const override { return enable_command_stats_; }
  ReadPolicy readPolicy() const override { return read_policy_; }
  bool connectionRateLimitEnabled() const override { return connection_rate_limit_enabled_; }
  uint32_t connectionRateLimitPerSec() const override { return connection_rate_limit_per_sec_; }

private:
  const std::chrono::milliseconds op_timeout_;
  const bool enable_hashtagging_;
  const bool enable_redirection_;
  const uint32_t max_buffer_size_before_flush_;
  const std::chrono::milliseconds buffer_flush_timeout_;
  const uint32_t max_upstream_unknown_connections_;
  const bool enable_command_stats_;
  ReadPolicy read_policy_;
  bool connection_rate_limit_enabled_;
  uint32_t connection_rate_limit_per_sec_;
};

class ClientImpl : public Client, public DecoderCallbacks, public Network::ConnectionCallbacks {
public:
  static ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                          EncoderPtr&& encoder, DecoderFactory& decoder_factory,
                          const ConfigSharedPtr& config,
                          const RedisCommandStatsSharedPtr& redis_command_stats,
                          Stats::Scope& scope, bool is_transaction_client);

  ClientImpl(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, EncoderPtr&& encoder,
             DecoderFactory& decoder_factory, const ConfigSharedPtr& config,
             const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
             bool is_transaction_client);
  ~ClientImpl() override;

  // Client
  void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) override {
    connection_->addConnectionCallbacks(callbacks);
  }
  void close() override;
  PoolRequest* makeRequest(const RespValue& request, ClientCallbacks& callbacks) override;
  bool active() override { return !pending_requests_.empty(); }
  void flushBufferAndResetTimer();
  void initialize(const std::string& auth_username, const std::string& auth_password) override;

private:
  friend class RedisClientImplTest;

  struct UpstreamReadFilter : public Network::ReadFilterBaseImpl {
    UpstreamReadFilter(ClientImpl& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::Continue;
    }

    ClientImpl& parent_;
  };

  struct PendingRequest : public PoolRequest {
    PendingRequest(ClientImpl& parent, ClientCallbacks& callbacks, Stats::StatName stat_name);
    ~PendingRequest() override;

    // PoolRequest
    void cancel() override;

    ClientImpl& parent_;
    ClientCallbacks& callbacks_;
    Stats::StatName command_;
    bool canceled_{};
    Stats::TimespanPtr aggregate_request_timer_;
    Stats::TimespanPtr command_request_timer_;
  };

  void onConnectOrOpTimeout();
  void onData(Buffer::Instance& data);
  void putOutlierEvent(Upstream::Outlier::Result result);

  // DecoderCallbacks
  void onRespValue(RespValuePtr&& value) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  Upstream::HostConstSharedPtr host_;
  Network::ClientConnectionPtr connection_;
  EncoderPtr encoder_;
  Buffer::OwnedImpl encoder_buffer_;
  DecoderPtr decoder_;
  const ConfigSharedPtr config_;
  std::list<PendingRequest> pending_requests_;
  Event::TimerPtr connect_or_op_timer_;
  bool connected_{};
  Event::TimerPtr flush_timer_;
  Envoy::TimeSource& time_source_;
  const RedisCommandStatsSharedPtr redis_command_stats_;
  Stats::Scope& scope_;
  bool is_transaction_client_;
};

class ClientFactoryImpl : public ClientFactory {
public:
  // RedisProxy::ConnPool::ClientFactoryImpl
  ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                   const ConfigSharedPtr& config,
                   const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
                   const std::string& auth_username, const std::string& auth_password,
                   bool is_transaction_client) override;

  static ClientFactoryImpl instance_;

private:
  DecoderFactoryImpl decoder_factory_;
};

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
