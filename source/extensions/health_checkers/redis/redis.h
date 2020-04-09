#pragma once

#include <chrono>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "common/upstream/health_checker_base_impl.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/redis_proxy/config.h"
#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

/**
 * Redis health checker implementation. Sends PING and expects PONG.
 */
class RedisHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  RedisHealthChecker(
      const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
      const envoy::config::health_checker::redis::v2::Redis& redis_config,
      Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
      Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api,
      Extensions::NetworkFilters::Common::Redis::Client::ClientFactory& client_factory);

  static const NetworkFilters::Common::Redis::RespValue& pingHealthCheckRequest() {
    static HealthCheckRequest* request = new HealthCheckRequest();
    return request->request_;
  }

  static const NetworkFilters::Common::Redis::RespValue&
  existsHealthCheckRequest(const std::string& key) {
    static HealthCheckRequest* request = new HealthCheckRequest(key);
    return request->request_;
  }

protected:
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::REDIS;
  }

private:
  friend class RedisHealthCheckerTest;

  struct RedisActiveHealthCheckSession
      : public ActiveHealthCheckSession,
        public Extensions::NetworkFilters::Common::Redis::Client::Config,
        public Extensions::NetworkFilters::Common::Redis::Client::ClientCallbacks,
        public Network::ConnectionCallbacks {
    RedisActiveHealthCheckSession(RedisHealthChecker& parent, const Upstream::HostSharedPtr& host);
    ~RedisActiveHealthCheckSession() override;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // Extensions::NetworkFilters::Common::Redis::Client::Config
    bool disableOutlierEvents() const override { return true; }
    std::chrono::milliseconds opTimeout() const override {
      // Allow the main Health Check infra to control timeout.
      return parent_.timeout_ * 2;
    }
    bool enableHashtagging() const override { return false; }
    bool enableRedirection() const override {
      return true;
    } // Redirection errors are treated as check successes.
    NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
      return NetworkFilters::Common::Redis::Client::ReadPolicy::Master;
    }

    // Batching
    unsigned int maxBufferSizeBeforeFlush() const override {
      return 0;
    } // Forces an immediate flush
    std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
      return std::chrono::milliseconds(1);
    }

    uint32_t maxUpstreamUnknownConnections() const override { return 0; }
    bool enableCommandStats() const override { return false; }

    // Extensions::NetworkFilters::Common::Redis::Client::ClientCallbacks
    void onResponse(NetworkFilters::Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    bool onRedirection(NetworkFilters::Common::Redis::RespValuePtr&&, const std::string&,
                       bool) override;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    RedisHealthChecker& parent_;
    Extensions::NetworkFilters::Common::Redis::Client::ClientPtr client_;
    Extensions::NetworkFilters::Common::Redis::Client::PoolRequest* current_request_{};
    Extensions::NetworkFilters::Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
  };

  enum class Type { Ping, Exists };

  struct HealthCheckRequest {
    HealthCheckRequest(const std::string& key);
    HealthCheckRequest();

    NetworkFilters::Common::Redis::RespValue request_;
  };

  using RedisActiveHealthCheckSessionPtr = std::unique_ptr<RedisActiveHealthCheckSession>;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<RedisActiveHealthCheckSession>(*this, host);
  }

  Extensions::NetworkFilters::Common::Redis::Client::ClientFactory& client_factory_;
  Type type_;
  const std::string key_;
  const std::string auth_password_;
};

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
