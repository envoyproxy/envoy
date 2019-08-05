#pragma once

#include <chrono>

#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"

#include "common/upstream/health_checker_base_impl.h"

#include "extensions/filters/network/common/redis/client_impl.h"
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
      const Upstream::Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
      const envoy::config::health_checker::redis::v2::Redis& redis_config,
      Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
      Upstream::HealthCheckEventLoggerPtr&& event_logger,
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
  envoy::data::core::v2alpha::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v2alpha::HealthCheckerType::REDIS;
  }

private:
  friend class RedisHealthCheckerTest;

  struct RedisActiveHealthCheckSession
      : public ActiveHealthCheckSession,
        public Extensions::NetworkFilters::Common::Redis::Client::Config,
        public Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks,
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

    // Batching
    unsigned int maxBufferSizeBeforeFlush() const override {
      return 0;
    } // Forces an immediate flush
    std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
      return std::chrono::milliseconds(1);
    }

    uint32_t maxUpstreamUnknownConnections() const override { return 0; }

    // Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks
    void onResponse(NetworkFilters::Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    bool onRedirection(const NetworkFilters::Common::Redis::RespValue& value) override;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    RedisHealthChecker& parent_;
    Extensions::NetworkFilters::Common::Redis::Client::ClientPtr client_;
    Extensions::NetworkFilters::Common::Redis::Client::PoolRequest* current_request_{};
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
};

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
