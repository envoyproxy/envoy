#pragma once

#include <chrono>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/health_checkers/cached/v3/cached.pb.h"

#include "source/common/config/utility.h"
#include "source/common/upstream/health_checker_base_impl.h"
#include "source/extensions/health_checkers/cached/client.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

/**
 * Cached health checker implementation.
 */
class CachedHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  CachedHealthChecker(const Upstream::Cluster& cluster,
                      const envoy::config::core::v3::HealthCheck& config,
                      const envoy::extensions::health_checkers::cached::v3::Cached& cached_config,
                      Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                      Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api,
                      Singleton::Manager& singleton_manager, ClientFactory& client_factory);

protected:
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::CACHED;
  }

private:
  friend class CachedHealthCheckerTest;

  class CachedActiveHealthCheckSession : public ActiveHealthCheckSession {
  public:
    CachedActiveHealthCheckSession(CachedHealthChecker& parent,
                                   const Upstream::HostSharedPtr& host);
    ~CachedActiveHealthCheckSession() override;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    const uint32_t DELAY_INTERVAL_MS = 999;

  private:
    void onDelayedIntervalTimeout();

    CachedHealthChecker& parent_;
    const std::string& hostname_;
    Event::TimerPtr delayed_interval_timer_;
  };

  using CachedActiveHealthCheckSessionPtr = std::unique_ptr<CachedActiveHealthCheckSession>;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<CachedActiveHealthCheckSession>(*this, host);
  }

  Event::Dispatcher& dispatcher_;
  ClientPtr client_;
};

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
