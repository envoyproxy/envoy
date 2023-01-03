#include "source/extensions/health_checkers/cached/cached.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/health_checkers/cached/v3/cached.pb.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

// Helper functions to get the correct hostname for a cached health check.
const std::string& getHostname(const Upstream::HostSharedPtr& host) {
  return host->hostnameForHealthChecks().empty() ? host->hostname()
                                                 : host->hostnameForHealthChecks();
}

ConnectionOptionsPtr
getConnectionOptions(const envoy::extensions::health_checkers::cached::v3::Cached& config) {
  ConnectionOptions opts;
  opts.host = config.host();
  opts.port = config.port();
  if (!config.user().empty())
    opts.user = config.user();
  opts.password = config.password();
  opts.db = config.db();
  opts.connect_timeout =
      std::chrono::milliseconds(DurationUtil::durationToMilliseconds(config.connect_timeout()));
  opts.command_timeout =
      std::chrono::milliseconds(DurationUtil::durationToMilliseconds(config.command_timeout()));
  if (config.has_tls_options()) {
    opts.tls.enabled = config.tls_options().enabled();
    opts.tls.cacert = config.tls_options().cacert();
    opts.tls.capath = config.tls_options().capath();
    opts.tls.cert = config.tls_options().cert();
    opts.tls.key = config.tls_options().key();
    opts.tls.sni = config.tls_options().sni();
  }

  return std::make_shared<ConnectionOptions>(opts);
}

CachedHealthChecker::CachedHealthChecker(
    const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    const envoy::extensions::health_checkers::cached::v3::Cached& cached_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
    Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api,
    Singleton::Manager& singleton_manager, ClientFactory& client_factory)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, api.randomGenerator(),
                            std::move(event_logger)),
      dispatcher_(dispatcher),
      client_(client_factory.create(singleton_manager, getConnectionOptions(cached_config),
                                    dispatcher, api.randomGenerator())) {}

CachedHealthChecker::CachedActiveHealthCheckSession::CachedActiveHealthCheckSession(
    CachedHealthChecker& parent, const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent), hostname_(getHostname(host)) {
  ENVOY_LOG(trace, "CachedActiveHealthCheckSession construct hostname={}", hostname_);
  delayed_interval_timer_ =
      parent_.dispatcher_.createTimer([this]() -> void { onDelayedIntervalTimeout(); });
  parent_.client_->start(hostname_);
}

CachedHealthChecker::CachedActiveHealthCheckSession::~CachedActiveHealthCheckSession() {
  ENVOY_LOG(trace, "CachedActiveHealthCheckSession destruct");
  if (delayed_interval_timer_ && delayed_interval_timer_->enabled()) {
    delayed_interval_timer_->disableTimer();
  }
  parent_.client_->close(hostname_);
}

void CachedHealthChecker::CachedActiveHealthCheckSession::onDeferredDelete() {
  ENVOY_LOG(trace, "CachedActiveHealthCheckSession onDeferredDelete");
  if (delayed_interval_timer_ && delayed_interval_timer_->enabled()) {
    delayed_interval_timer_->disableTimer();
  }
  parent_.client_->close(hostname_);
}

void CachedHealthChecker::CachedActiveHealthCheckSession::onInterval() {
  ENVOY_LOG(trace, "CachedActiveHealthCheckSession onInterval");
  delayed_interval_timer_->enableTimer(std::chrono::milliseconds(DELAY_INTERVAL_MS));
}

void CachedHealthChecker::CachedActiveHealthCheckSession::onDelayedIntervalTimeout() {
  delayed_interval_timer_->disableTimer();
  bool is_healthy = parent_.client_->sendRequest(hostname_);
  ENVOY_LOG(trace,
            "CachedActiveHealthCheckSession onDelayedIntervalTimeout. hostname: {}, is_healthy: {}",
            hostname_, is_healthy);
  if (is_healthy) {
    handleSuccess();
  } else {
    handleFailure(envoy::data::core::v3::ACTIVE);
  }
}

void CachedHealthChecker::CachedActiveHealthCheckSession::onTimeout() {
  ENVOY_LOG(trace, "CachedActiveHealthCheckSession onTimeout");
}

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
