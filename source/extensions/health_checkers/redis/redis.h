#pragma once

#include <chrono>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/redis/v3/redis.pb.h"

#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/redis_proxy/config.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "source/extensions/health_checkers/common/health_checker_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

/**
 * All redis health checker stats. @see stats_macros.h
 */
#define ALL_REDIS_HEALTH_CHECKER_STATS(COUNTER) COUNTER(exists_failure)

/**
 * Definition of all redis health checker stats. @see stats_macros.h
 */
struct RedisHealthCheckerStats {
  ALL_REDIS_HEALTH_CHECKER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Redis health checker implementation. Sends PING and expects PONG.
 */
class RedisHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  RedisHealthChecker(
      const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
      const envoy::extensions::health_checkers::redis::v3::Redis& redis_config,
      Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
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
  RedisHealthCheckerStats generateRedisStats(Stats::Scope& scope);

  struct RedisConfig : public Extensions::NetworkFilters::Common::Redis::Client::Config {
    RedisConfig(std::chrono::milliseconds timeout) : parent_timeout_(timeout) {}

    // Extensions::NetworkFilters::Common::Redis::Client::Config
    bool disableOutlierEvents() const override { return true; }
    std::chrono::milliseconds opTimeout() const override {
      // Allow the main Health Check infra to control timeout.
      return parent_timeout_ * 2;
    }
    bool enableHashtagging() const override { return false; }
    bool enableRedirection() const override {
      return true;
    } // Redirection errors are treated as check successes.
    NetworkFilters::Common::Redis::Client::ReadPolicy readPolicy() const override {
      return NetworkFilters::Common::Redis::Client::ReadPolicy::Primary;
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
    bool connectionRateLimitEnabled() const override { return false; }
    uint32_t connectionRateLimitPerSec() const override { return 0; }

    const std::chrono::milliseconds parent_timeout_;
  };

  struct RedisActiveHealthCheckSession
      : public ActiveHealthCheckSession,
        public Extensions::NetworkFilters::Common::Redis::Client::ClientCallbacks,
        public Network::ConnectionCallbacks {
    RedisActiveHealthCheckSession(RedisHealthChecker& parent, const Upstream::HostSharedPtr& host);
    ~RedisActiveHealthCheckSession() override;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // Extensions::NetworkFilters::Common::Redis::Client::ClientCallbacks
    void onResponse(NetworkFilters::Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    void onRedirection(NetworkFilters::Common::Redis::RespValuePtr&&, const std::string&,
                       bool) override;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    RedisHealthChecker& parent_;
    std::shared_ptr<RedisConfig> redis_config_{std::make_shared<RedisConfig>(parent_.timeout_)};
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
  RedisHealthCheckerStats redis_stats_;
  const std::string auth_username_;
  const std::string auth_password_;
};

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
