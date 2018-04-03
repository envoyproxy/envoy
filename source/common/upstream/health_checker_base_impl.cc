#include "common/upstream/health_checker_base_impl.h"

#include "common/router/router.h"

namespace Envoy {
namespace Upstream {

HealthCheckerImplBase::HealthCheckerImplBase(const Cluster& cluster,
                                             const envoy::api::v2::core::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random)
    : cluster_(cluster), dispatcher_(dispatcher),
      timeout_(PROTOBUF_GET_MS_REQUIRED(config, timeout)),
      unhealthy_threshold_(PROTOBUF_GET_WRAPPED_REQUIRED(config, unhealthy_threshold)),
      healthy_threshold_(PROTOBUF_GET_WRAPPED_REQUIRED(config, healthy_threshold)),
      stats_(generateStats(cluster.info()->statsScope())), runtime_(runtime), random_(random),
      reuse_connection_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, reuse_connection, true)),
      interval_(PROTOBUF_GET_MS_REQUIRED(config, interval)),
      no_traffic_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, no_traffic_interval, 60000)),
      interval_jitter_(PROTOBUF_GET_MS_OR_DEFAULT(config, interval_jitter, 0)) {
  cluster_.prioritySet().addMemberUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) -> void {
        onClusterMemberUpdate(hosts_added, hosts_removed);
      });
}

void HealthCheckerImplBase::decHealthy() {
  ASSERT(local_process_healthy_ > 0);
  local_process_healthy_--;
  refreshHealthyStat();
}

HealthCheckerStats HealthCheckerImplBase::generateStats(Stats::Scope& scope) {
  std::string prefix("health_check.");
  return {ALL_HEALTH_CHECKER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                   POOL_GAUGE_PREFIX(scope, prefix))};
}

void HealthCheckerImplBase::incHealthy() {
  local_process_healthy_++;
  refreshHealthyStat();
}

std::chrono::milliseconds HealthCheckerImplBase::interval() const {
  // See if the cluster has ever made a connection. If so, we use the defined HC interval. If not,
  // we use a much slower interval to keep the host info relatively up to date in case we suddenly
  // start sending traffic to this cluster. In general host updates are rare and this should
  // greatly smooth out needless health checking.
  uint64_t base_time_ms;
  if (cluster_.info()->stats().upstream_cx_total_.used()) {
    base_time_ms = interval_.count();
  } else {
    base_time_ms = no_traffic_interval_.count();
  }

  if (interval_jitter_.count() > 0) {
    base_time_ms += (random_.random() % interval_jitter_.count());
  }

  uint64_t min_interval = runtime_.snapshot().getInteger("health_check.min_interval", 0);
  uint64_t max_interval = runtime_.snapshot().getInteger("health_check.max_interval",
                                                         std::numeric_limits<uint64_t>::max());

  uint64_t final_ms = std::min(base_time_ms, max_interval);
  final_ms = std::max(final_ms, min_interval);
  return std::chrono::milliseconds(final_ms);
}

void HealthCheckerImplBase::addHosts(const HostVector& hosts) {
  for (const HostSharedPtr& host : hosts) {
    active_sessions_[host] = makeSession(host);
    host->setHealthChecker(
        HealthCheckHostMonitorPtr{new HealthCheckHostMonitorImpl(shared_from_this(), host)});
    active_sessions_[host]->start();
  }
}

void HealthCheckerImplBase::onClusterMemberUpdate(const HostVector& hosts_added,
                                                  const HostVector& hosts_removed) {
  addHosts(hosts_added);
  for (const HostSharedPtr& host : hosts_removed) {
    auto session_iter = active_sessions_.find(host);
    ASSERT(active_sessions_.end() != session_iter);
    active_sessions_.erase(session_iter);
  }
}

void HealthCheckerImplBase::refreshHealthyStat() {
  // Each hot restarted process health checks independently. To make the stats easier to read,
  // we assume that both processes will converge and the last one that writes wins for the host.
  stats_.healthy_.set(local_process_healthy_);
}

void HealthCheckerImplBase::runCallbacks(HostSharedPtr host, bool changed_state) {
  // When a parent process shuts down, it will kill all of the active health checking sessions,
  // which will decrement the healthy count and the healthy stat in the parent. If the child is
  // stable and does not update, the healthy stat will be wrong. This routine is called any time
  // any HC happens against a host so just refresh the healthy stat here so that it is correct.
  refreshHealthyStat();

  for (const HostStatusCb& cb : callbacks_) {
    cb(host, changed_state);
  }
}

void HealthCheckerImplBase::HealthCheckHostMonitorImpl::setUnhealthy() {
  // This is called cross thread. The cluster/health checker might already be gone.
  std::shared_ptr<HealthCheckerImplBase> health_checker = health_checker_.lock();
  if (health_checker) {
    health_checker->setUnhealthyCrossThread(host_.lock());
  }
}

void HealthCheckerImplBase::setUnhealthyCrossThread(const HostSharedPtr& host) {
  // The threading here is complex. The cluster owns the only strong reference to the health
  // checker. It might go away when we post to the main thread from a worker thread. To deal with
  // this we use the following sequence of events:
  // 1) We capture a weak reference to the health checker and post it from worker thread to main
  //    thread.
  // 2) On the main thread, we make sure it is still valid (as the cluster may have been destroyed).
  // 3) Additionally, the host/session may also be gone by then so we check that also.
  std::weak_ptr<HealthCheckerImplBase> weak_this = shared_from_this();
  dispatcher_.post([weak_this, host]() -> void {
    std::shared_ptr<HealthCheckerImplBase> shared_this = weak_this.lock();
    if (shared_this == nullptr) {
      return;
    }

    const auto session = shared_this->active_sessions_.find(host);
    if (session == shared_this->active_sessions_.end()) {
      return;
    }

    session->second->setUnhealthy(ActiveHealthCheckSession::FailureType::Passive);
  });
}

void HealthCheckerImplBase::start() {
  for (auto& host_set : cluster_.prioritySet().hostSetsPerPriority()) {
    addHosts(host_set->hosts());
  }
}

HealthCheckerImplBase::ActiveHealthCheckSession::ActiveHealthCheckSession(
    HealthCheckerImplBase& parent, HostSharedPtr host)
    : host_(host), parent_(parent),
      interval_timer_(parent.dispatcher_.createTimer([this]() -> void { onIntervalBase(); })),
      timeout_timer_(parent.dispatcher_.createTimer([this]() -> void { onTimeoutBase(); })) {

  if (!host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    parent.incHealthy();
  }
}

HealthCheckerImplBase::ActiveHealthCheckSession::~ActiveHealthCheckSession() {
  if (!host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    parent_.decHealthy();
  }
}

void HealthCheckerImplBase::ActiveHealthCheckSession::handleSuccess() {
  // If we are healthy, reset the # of unhealthy to zero.
  num_unhealthy_ = 0;

  bool changed_state = false;
  if (host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    // If this is the first time we ever got a check result on this host, we immediately move
    // it to healthy. This makes startup faster with a small reduction in overall reliability
    // depending on the HC settings.
    if (first_check_ || ++num_healthy_ == parent_.healthy_threshold_) {
      host_->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      parent_.incHealthy();
      changed_state = true;
    }
  }

  parent_.stats_.success_.inc();
  first_check_ = false;
  parent_.runCallbacks(host_, changed_state);

  timeout_timer_->disableTimer();
  interval_timer_->enableTimer(parent_.interval());
}

void HealthCheckerImplBase::ActiveHealthCheckSession::setUnhealthy(FailureType type) {
  // If we are unhealthy, reset the # of healthy to zero.
  num_healthy_ = 0;

  bool changed_state = false;
  if (!host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    if (type != FailureType::Network || ++num_unhealthy_ == parent_.unhealthy_threshold_) {
      host_->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
      parent_.decHealthy();
      changed_state = true;
    }
  }

  parent_.stats_.failure_.inc();
  if (type == FailureType::Network) {
    parent_.stats_.network_failure_.inc();
  } else if (type == FailureType::Passive) {
    parent_.stats_.passive_failure_.inc();
  }

  first_check_ = false;
  parent_.runCallbacks(host_, changed_state);
}

void HealthCheckerImplBase::ActiveHealthCheckSession::handleFailure(FailureType type) {
  setUnhealthy(type);
  timeout_timer_->disableTimer();
  interval_timer_->enableTimer(parent_.interval());
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onIntervalBase() {
  onInterval();
  timeout_timer_->enableTimer(parent_.timeout_);
  parent_.stats_.attempt_.inc();
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onTimeoutBase() {
  onTimeout();
  handleFailure(FailureType::Network);
}

} // namespace Upstream
} // namespace Envoy