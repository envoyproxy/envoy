#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/type/matcher/string.pb.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/logger.h"
#include "common/common/matchers.h"
#include "common/network/transport_socket_options_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * All health checker stats. @see stats_macros.h
 */
#define ALL_HEALTH_CHECKER_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(attempt)                                                                                 \
  COUNTER(failure)                                                                                 \
  COUNTER(network_failure)                                                                         \
  COUNTER(passive_failure)                                                                         \
  COUNTER(success)                                                                                 \
  COUNTER(verify_cluster)                                                                          \
  GAUGE(degraded, Accumulate)                                                                      \
  GAUGE(healthy, Accumulate)

/**
 * Definition of all health checker stats. @see stats_macros.h
 */
struct HealthCheckerStats {
  ALL_HEALTH_CHECKER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Base implementation for all health checkers.
 */
class HealthCheckerImplBase : public HealthChecker,
                              protected Logger::Loggable<Logger::Id::hc>,
                              public std::enable_shared_from_this<HealthCheckerImplBase> {
public:
  // Upstream::HealthChecker
  void addHostCheckCompleteCb(HostStatusCb callback) override { callbacks_.push_back(callback); }
  void start() override;
  std::shared_ptr<const Network::TransportSocketOptionsImpl> transportSocketOptions() const {
    return transport_socket_options_;
  }

protected:
  class ActiveHealthCheckSession : public Event::DeferredDeletable {
  public:
    ~ActiveHealthCheckSession() override;
    HealthTransition setUnhealthy(envoy::data::core::v3::HealthCheckFailureType type);
    void onDeferredDeleteBase();
    void start() { onInitialInterval(); }

  protected:
    ActiveHealthCheckSession(HealthCheckerImplBase& parent, HostSharedPtr host);

    void handleSuccess(bool degraded = false);
    void handleDegraded();
    void handleFailure(envoy::data::core::v3::HealthCheckFailureType type);

    HostSharedPtr host_;

  private:
    // Clears the pending flag if it is set. By clearing this flag we're marking the host as having
    // been health checked.
    // Returns the changed state to use following the flag update.
    HealthTransition clearPendingFlag(HealthTransition changed_state);
    virtual void onInterval() PURE;
    void onIntervalBase();
    virtual void onTimeout() PURE;
    void onTimeoutBase();
    virtual void onDeferredDelete() PURE;
    void onInitialInterval();

    HealthCheckerImplBase& parent_;
    Event::TimerPtr interval_timer_;
    Event::TimerPtr timeout_timer_;
    uint32_t num_unhealthy_{};
    uint32_t num_healthy_{};
    bool first_check_{true};
  };

  using ActiveHealthCheckSessionPtr = std::unique_ptr<ActiveHealthCheckSession>;

  HealthCheckerImplBase(const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Runtime::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);
  ~HealthCheckerImplBase() override;

  virtual ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) PURE;
  virtual envoy::data::core::v3::HealthCheckerType healthCheckerType() const PURE;

  const bool always_log_health_check_failures_;
  const Cluster& cluster_;
  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds timeout_;
  const uint32_t unhealthy_threshold_;
  const uint32_t healthy_threshold_;
  HealthCheckerStats stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const bool reuse_connection_;
  HealthCheckEventLoggerPtr event_logger_;

private:
  struct HealthCheckHostMonitorImpl : public HealthCheckHostMonitor {
    HealthCheckHostMonitorImpl(const std::shared_ptr<HealthCheckerImplBase>& health_checker,
                               const HostSharedPtr& host)
        : health_checker_(health_checker), host_(host) {}

    // Upstream::HealthCheckHostMonitor
    void setUnhealthy() override;

    std::weak_ptr<HealthCheckerImplBase> health_checker_;
    std::weak_ptr<Host> host_;
  };

  void addHosts(const HostVector& hosts);
  void decHealthy();
  void decDegraded();
  HealthCheckerStats generateStats(Stats::Scope& scope);
  void incHealthy();
  void incDegraded();
  std::chrono::milliseconds interval(HealthState state, HealthTransition changed_state) const;
  std::chrono::milliseconds intervalWithJitter(uint64_t base_time_ms,
                                               std::chrono::milliseconds interval_jitter) const;
  void onClusterMemberUpdate(const HostVector& hosts_added, const HostVector& hosts_removed);
  void refreshHealthyStat();
  void runCallbacks(HostSharedPtr host, HealthTransition changed_state);
  void setUnhealthyCrossThread(const HostSharedPtr& host);
  static std::shared_ptr<const Network::TransportSocketOptionsImpl>
  initTransportSocketOptions(const envoy::config::core::v3::HealthCheck& config);

  static const std::chrono::milliseconds NO_TRAFFIC_INTERVAL;

  std::list<HostStatusCb> callbacks_;
  const std::chrono::milliseconds interval_;
  const std::chrono::milliseconds no_traffic_interval_;
  const std::chrono::milliseconds initial_jitter_;
  const std::chrono::milliseconds interval_jitter_;
  const uint32_t interval_jitter_percent_;
  const std::chrono::milliseconds unhealthy_interval_;
  const std::chrono::milliseconds unhealthy_edge_interval_;
  const std::chrono::milliseconds healthy_edge_interval_;
  std::unordered_map<HostSharedPtr, ActiveHealthCheckSessionPtr> active_sessions_;
  uint64_t local_process_healthy_{};
  uint64_t local_process_degraded_{};
  const std::shared_ptr<const Network::TransportSocketOptionsImpl> transport_socket_options_;
};

class HealthCheckEventLoggerImpl : public HealthCheckEventLogger {
public:
  HealthCheckEventLoggerImpl(AccessLog::AccessLogManager& log_manager, TimeSource& time_source,
                             const std::string& file_name)
      : time_source_(time_source), file_(log_manager.createAccessLog(file_name)) {}

  void logEjectUnhealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                         const HostDescriptionConstSharedPtr& host,
                         envoy::data::core::v3::HealthCheckFailureType failure_type) override;
  void logAddHealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                     const HostDescriptionConstSharedPtr& host, bool first_check) override;
  void logUnhealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                    const HostDescriptionConstSharedPtr& host,
                    envoy::data::core::v3::HealthCheckFailureType failure_type,
                    bool first_check) override;
  void logDegraded(envoy::data::core::v3::HealthCheckerType health_checker_type,
                   const HostDescriptionConstSharedPtr& host) override;
  void logNoLongerDegraded(envoy::data::core::v3::HealthCheckerType health_checker_type,
                           const HostDescriptionConstSharedPtr& host) override;

private:
  void createHealthCheckEvent(
      envoy::data::core::v3::HealthCheckerType health_checker_type, const HostDescription& host,
      std::function<void(envoy::data::core::v3::HealthCheckEvent&)> callback) const;
  TimeSource& time_source_;
  AccessLog::AccessLogFileSharedPtr file_;
};

} // namespace Upstream
} // namespace Envoy
