#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/common/callback.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/type/matcher/string.pb.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/network/transport_socket_options_impl.h"

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
  MetadataConstSharedPtr transportSocketMatchMetadata() const {
    return transport_socket_match_metadata_;
  }

protected:
  class ActiveHealthCheckSession : public Event::DeferredDeletable {
  public:
    ~ActiveHealthCheckSession() override;
    HealthTransition setUnhealthy(envoy::data::core::v3::HealthCheckFailureType type,
                                  bool retriable);
    void onDeferredDeleteBase();
    void start() { onInitialInterval(); }

  protected:
    ActiveHealthCheckSession(HealthCheckerImplBase& parent, HostSharedPtr host);

    void handleSuccess(bool degraded = false);
    void handleFailure(envoy::data::core::v3::HealthCheckFailureType type, bool retriable = false);

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
    TimeSource& time_source_;
  };

  using ActiveHealthCheckSessionPtr = std::unique_ptr<ActiveHealthCheckSession>;

  HealthCheckerImplBase(const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                        Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                        Random::RandomGenerator& random, HealthCheckEventLoggerPtr&& event_logger);
  ~HealthCheckerImplBase() override;

  virtual ActiveHealthCheckSessionPtr makeSession(HostSharedPtr host) PURE;
  virtual envoy::data::core::v3::HealthCheckerType healthCheckerType() const PURE;

  const bool always_log_health_check_failures_;
  const bool always_log_health_check_success_;
  const Cluster& cluster_;
  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds timeout_;
  const uint32_t unhealthy_threshold_;
  const uint32_t healthy_threshold_;
  HealthCheckerStats stats_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  const bool reuse_connection_;
  HealthCheckEventLoggerPtr event_logger_;

private:
  struct HealthCheckHostMonitorImpl : public HealthCheckHostMonitor {
    HealthCheckHostMonitorImpl(const std::shared_ptr<HealthCheckerImplBase>& health_checker,
                               const HostSharedPtr& host)
        : health_checker_(health_checker), host_(host) {}

    // Upstream::HealthCheckHostMonitor
    void setUnhealthy(UnhealthyType type) override;

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
  void runCallbacks(HostSharedPtr host, HealthTransition changed_state,
                    HealthState current_check_result);
  void setUnhealthyCrossThread(const HostSharedPtr& host,
                               HealthCheckHostMonitor::UnhealthyType type);
  static std::shared_ptr<const Network::TransportSocketOptionsImpl>
  initTransportSocketOptions(const envoy::config::core::v3::HealthCheck& config);
  static MetadataConstSharedPtr
  initTransportSocketMatchMetadata(const envoy::config::core::v3::HealthCheck& config);

  std::list<HostStatusCb> callbacks_;
  const std::chrono::milliseconds interval_;
  const std::chrono::milliseconds no_traffic_interval_;
  const std::chrono::milliseconds no_traffic_healthy_interval_;
  const std::chrono::milliseconds initial_jitter_;
  const std::chrono::milliseconds interval_jitter_;
  const uint32_t interval_jitter_percent_;
  const std::chrono::milliseconds unhealthy_interval_;
  const std::chrono::milliseconds unhealthy_edge_interval_;
  const std::chrono::milliseconds healthy_edge_interval_;
  absl::node_hash_map<HostSharedPtr, ActiveHealthCheckSessionPtr> active_sessions_;
  const std::shared_ptr<const Network::TransportSocketOptionsImpl> transport_socket_options_;
  const MetadataConstSharedPtr transport_socket_match_metadata_;
  const Common::CallbackHandlePtr member_update_cb_;
};

} // namespace Upstream
} // namespace Envoy
