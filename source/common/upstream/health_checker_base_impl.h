#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/core/health_check.pb.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/health_checker.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Upstream {

/**
 * All health checker stats. @see stats_macros.h
 */
// clang-format off
#define ALL_HEALTH_CHECKER_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(attempt)                                                                                 \
  COUNTER(success)                                                                                 \
  COUNTER(failure)                                                                                 \
  COUNTER(passive_failure)                                                                         \
  COUNTER(network_failure)                                                                         \
  COUNTER(verify_cluster)                                                                          \
  GAUGE  (healthy)
// clang-format on

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

protected:
  class ActiveHealthCheckSession {
  public:
    virtual ~ActiveHealthCheckSession();
    HealthTransition setUnhealthy(envoy::data::core::v2alpha::HealthCheckFailureType type);
    void start() { onIntervalBase(); }

  protected:
    ActiveHealthCheckSession(HealthCheckerImplBase& parent, HostSharedPtr host);

    void handleSuccess();
    void handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType type);

    HostSharedPtr host_;

  private:
    virtual void onInterval() PURE;
    void onIntervalBase();
    virtual void onTimeout() PURE;
    void onTimeoutBase();

    HealthCheckerImplBase& parent_;
    Event::TimerPtr interval_timer_;
    Event::TimerPtr timeout_timer_;
    uint32_t num_unhealthy_{};
    uint32_t num_healthy_{};
    bool first_check_{true};
  };

  typedef std::unique_ptr<ActiveHealthCheckSession> ActiveHealthCheckSessionPtr;

  HealthCheckerImplBase(const Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Runtime::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);

  virtual ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) PURE;
  virtual envoy::data::core::v2alpha::HealthCheckerType healthCheckerType() const PURE;

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
  HealthCheckerStats generateStats(Stats::Scope& scope);
  void incHealthy();
  std::chrono::milliseconds interval(HealthState state, HealthTransition changed_state) const;
  void onClusterMemberUpdate(const HostVector& hosts_added, const HostVector& hosts_removed);
  void refreshHealthyStat();
  void runCallbacks(HostSharedPtr host, HealthTransition changed_state);
  void setUnhealthyCrossThread(const HostSharedPtr& host);

  static const std::chrono::milliseconds NO_TRAFFIC_INTERVAL;

  std::list<HostStatusCb> callbacks_;
  const std::chrono::milliseconds interval_;
  const std::chrono::milliseconds no_traffic_interval_;
  const std::chrono::milliseconds interval_jitter_;
  const std::chrono::milliseconds unhealthy_interval_;
  const std::chrono::milliseconds unhealthy_edge_interval_;
  const std::chrono::milliseconds healthy_edge_interval_;
  std::unordered_map<HostSharedPtr, ActiveHealthCheckSessionPtr> active_sessions_;
  uint64_t local_process_healthy_{};
};

class HealthCheckEventLoggerImpl : public HealthCheckEventLogger {
public:
  HealthCheckEventLoggerImpl(AccessLog::AccessLogManager& log_manager, const std::string& file_name)
      : file_(log_manager.createAccessLog(file_name)) {}

  void logEjectUnhealthy(envoy::data::core::v2alpha::HealthCheckerType health_checker_type,
                         const HostDescriptionConstSharedPtr& host,
                         envoy::data::core::v2alpha::HealthCheckFailureType failure_type) override;
  void logAddHealthy(envoy::data::core::v2alpha::HealthCheckerType health_checker_type,
                     const HostDescriptionConstSharedPtr& host, bool first_check) override;

private:
  Filesystem::FileSharedPtr file_;
};

} // namespace Upstream
} // namespace Envoy
