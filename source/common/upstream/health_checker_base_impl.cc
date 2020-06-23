#include "common/upstream/health_checker_base_impl.h"

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/stats/scope.h"

#include "common/network/utility.h"
#include "common/router/router.h"

namespace Envoy {
namespace Upstream {

HealthCheckerImplBase::HealthCheckerImplBase(const Cluster& cluster,
                                             const envoy::config::core::v3::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random,
                                             HealthCheckEventLoggerPtr&& event_logger)
    : always_log_health_check_failures_(config.always_log_health_check_failures()),
      cluster_(cluster), dispatcher_(dispatcher),
      timeout_(PROTOBUF_GET_MS_REQUIRED(config, timeout)),
      unhealthy_threshold_(PROTOBUF_GET_WRAPPED_REQUIRED(config, unhealthy_threshold)),
      healthy_threshold_(PROTOBUF_GET_WRAPPED_REQUIRED(config, healthy_threshold)),
      stats_(generateStats(cluster.info()->statsScope())), runtime_(runtime), random_(random),
      reuse_connection_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, reuse_connection, true)),
      event_logger_(std::move(event_logger)), interval_(PROTOBUF_GET_MS_REQUIRED(config, interval)),
      no_traffic_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, no_traffic_interval, 60000)),
      initial_jitter_(PROTOBUF_GET_MS_OR_DEFAULT(config, initial_jitter, 0)),
      interval_jitter_(PROTOBUF_GET_MS_OR_DEFAULT(config, interval_jitter, 0)),
      interval_jitter_percent_(config.interval_jitter_percent()),
      unhealthy_interval_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, unhealthy_interval, interval_.count())),
      unhealthy_edge_interval_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, unhealthy_edge_interval, unhealthy_interval_.count())),
      healthy_edge_interval_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, healthy_edge_interval, interval_.count())),
      transport_socket_options_(initTransportSocketOptions(config)),
      transport_socket_match_metadata_(initTransportSocketMatchMetadata(config)) {
  cluster_.prioritySet().addMemberUpdateCb(
      [this](const HostVector& hosts_added, const HostVector& hosts_removed) -> void {
        onClusterMemberUpdate(hosts_added, hosts_removed);
      });
}

std::shared_ptr<const Network::TransportSocketOptionsImpl>
HealthCheckerImplBase::initTransportSocketOptions(
    const envoy::config::core::v3::HealthCheck& config) {
  if (config.has_tls_options()) {
    std::vector<std::string> protocols{config.tls_options().alpn_protocols().begin(),
                                       config.tls_options().alpn_protocols().end()};
    return std::make_shared<const Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::move(protocols));
  }

  return std::make_shared<const Network::TransportSocketOptionsImpl>();
}

MetadataConstSharedPtr HealthCheckerImplBase::initTransportSocketMatchMetadata(
    const envoy::config::core::v3::HealthCheck& config) {
  if (config.has_transport_socket_match_criteria()) {
    std::shared_ptr<envoy::config::core::v3::Metadata> metadata =
        std::make_shared<envoy::config::core::v3::Metadata>();
    (*metadata->mutable_filter_metadata())[Envoy::Config::MetadataFilters::get()
                                               .ENVOY_TRANSPORT_SOCKET_MATCH] =
        config.transport_socket_match_criteria();
    return metadata;
  }

  return nullptr;
}

HealthCheckerImplBase::~HealthCheckerImplBase() {
  // ASSERTs inside the session destructor check to make sure we have been previously deferred
  // deleted. Unify that logic here before actual destruction happens.
  for (auto& session : active_sessions_) {
    session.second->onDeferredDeleteBase();
  }
}

void HealthCheckerImplBase::decHealthy() { stats_.healthy_.sub(1); }

void HealthCheckerImplBase::decDegraded() { stats_.degraded_.sub(1); }

HealthCheckerStats HealthCheckerImplBase::generateStats(Stats::Scope& scope) {
  std::string prefix("health_check.");
  return {ALL_HEALTH_CHECKER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                   POOL_GAUGE_PREFIX(scope, prefix))};
}

void HealthCheckerImplBase::incHealthy() { stats_.healthy_.add(1); }

void HealthCheckerImplBase::incDegraded() { stats_.degraded_.add(1); }

std::chrono::milliseconds HealthCheckerImplBase::interval(HealthState state,
                                                          HealthTransition changed_state) const {
  // See if the cluster has ever made a connection. If not, we use a much slower interval to keep
  // the host info relatively up to date in case we suddenly start sending traffic to this cluster.
  // In general host updates are rare and this should greatly smooth out needless health checking.
  // If a connection has been established, we choose an interval based on the host's health. Please
  // refer to the HealthCheck API documentation for more details.
  uint64_t base_time_ms;
  if (cluster_.info()->stats().upstream_cx_total_.used()) {
    // When healthy/unhealthy threshold is configured the health transition of a host will be
    // delayed. In this situation Envoy should use the edge interval settings between health checks.
    //
    // Example scenario for an unhealthy host with healthy_threshold set to 3:
    // - check fails, host is still unhealthy and next check happens after unhealthy_interval;
    // - check succeeds, host is still unhealthy and next check happens after healthy_edge_interval;
    // - check succeeds, host is still unhealthy and next check happens after healthy_edge_interval;
    // - check succeeds, host is now healthy and next check happens after interval;
    // - check succeeds, host is still healthy and next check happens after interval.
    switch (state) {
    case HealthState::Unhealthy:
      base_time_ms = changed_state == HealthTransition::ChangePending
                         ? unhealthy_edge_interval_.count()
                         : unhealthy_interval_.count();
      break;
    default:
      base_time_ms = changed_state == HealthTransition::ChangePending
                         ? healthy_edge_interval_.count()
                         : interval_.count();
      break;
    }
  } else {
    base_time_ms = no_traffic_interval_.count();
  }
  return intervalWithJitter(base_time_ms, interval_jitter_);
}

std::chrono::milliseconds
HealthCheckerImplBase::intervalWithJitter(uint64_t base_time_ms,
                                          std::chrono::milliseconds interval_jitter) const {
  const uint64_t jitter_percent_mod = interval_jitter_percent_ * base_time_ms / 100;
  if (jitter_percent_mod > 0) {
    base_time_ms += random_.random() % jitter_percent_mod;
  }

  if (interval_jitter.count() > 0) {
    base_time_ms += (random_.random() % interval_jitter.count());
  }

  const uint64_t min_interval = runtime_.snapshot().getInteger("health_check.min_interval", 0);
  const uint64_t max_interval = runtime_.snapshot().getInteger(
      "health_check.max_interval", std::numeric_limits<uint64_t>::max());

  uint64_t final_ms = std::min(base_time_ms, max_interval);
  // We force a non-zero final MS, to prevent live lock.
  final_ms = std::max(uint64_t(1), std::max(final_ms, min_interval));
  return std::chrono::milliseconds(final_ms);
}

void HealthCheckerImplBase::addHosts(const HostVector& hosts) {
  for (const HostSharedPtr& host : hosts) {
    active_sessions_[host] = makeSession(host);
    host->setActiveHealthFailureType(Host::ActiveHealthFailureType::UNKNOWN);
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
    // This deletion can happen inline in response to a host failure, so we deferred delete.
    session_iter->second->onDeferredDeleteBase();
    dispatcher_.deferredDelete(std::move(session_iter->second));
    active_sessions_.erase(session_iter);
  }
}

void HealthCheckerImplBase::runCallbacks(HostSharedPtr host, HealthTransition changed_state) {
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

    session->second->setUnhealthy(envoy::data::core::v3::PASSIVE);
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

  if (host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
    parent.incDegraded();
  }
}

HealthCheckerImplBase::ActiveHealthCheckSession::~ActiveHealthCheckSession() {
  // Make sure onDeferredDeleteBase() has been called. We should not reference our parent at this
  // point since we may have been deferred deleted.
  ASSERT(interval_timer_ == nullptr && timeout_timer_ == nullptr);
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onDeferredDeleteBase() {
  // The session is about to be deferred deleted. Make sure all timers are gone and any
  // implementation specific state is destroyed.
  interval_timer_.reset();
  timeout_timer_.reset();
  if (!host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    parent_.decHealthy();
  }
  if (host_->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
    parent_.decDegraded();
  }
  onDeferredDelete();
}

void HealthCheckerImplBase::ActiveHealthCheckSession::handleSuccess(bool degraded) {
  // If we are healthy, reset the # of unhealthy to zero.
  num_unhealthy_ = 0;

  HealthTransition changed_state = HealthTransition::Unchanged;
  if (host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    // If this is the first time we ever got a check result on this host, we immediately move
    // it to healthy. This makes startup faster with a small reduction in overall reliability
    // depending on the HC settings.
    if (first_check_ || ++num_healthy_ == parent_.healthy_threshold_) {
      host_->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      parent_.incHealthy();
      changed_state = HealthTransition::Changed;
      if (parent_.event_logger_) {
        parent_.event_logger_->logAddHealthy(parent_.healthCheckerType(), host_, first_check_);
      }
    } else {
      changed_state = HealthTransition::ChangePending;
    }
  }

  changed_state = clearPendingFlag(changed_state);

  if (degraded != host_->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
    if (degraded) {
      host_->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
      parent_.incDegraded();
      if (parent_.event_logger_) {
        parent_.event_logger_->logDegraded(parent_.healthCheckerType(), host_);
      }
    } else {
      if (parent_.event_logger_) {
        parent_.event_logger_->logNoLongerDegraded(parent_.healthCheckerType(), host_);
      }
      host_->healthFlagClear(Host::HealthFlag::DEGRADED_ACTIVE_HC);
    }

    // This check ensures that we honor the decision made about Changed vs ChangePending in the
    // above block.
    // TODO(snowp): should there be degraded_threshold?
    if (changed_state == HealthTransition::Unchanged) {
      changed_state = HealthTransition::Changed;
    }
  }

  parent_.stats_.success_.inc();
  first_check_ = false;
  parent_.runCallbacks(host_, changed_state);

  timeout_timer_->disableTimer();
  interval_timer_->enableTimer(parent_.interval(HealthState::Healthy, changed_state));
}

HealthTransition HealthCheckerImplBase::ActiveHealthCheckSession::setUnhealthy(
    envoy::data::core::v3::HealthCheckFailureType type) {
  // If we are unhealthy, reset the # of healthy to zero.
  num_healthy_ = 0;

  HealthTransition changed_state = HealthTransition::Unchanged;
  if (!host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    if (type != envoy::data::core::v3::NETWORK ||
        ++num_unhealthy_ == parent_.unhealthy_threshold_) {
      host_->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
      parent_.decHealthy();
      changed_state = HealthTransition::Changed;
      if (parent_.event_logger_) {
        parent_.event_logger_->logEjectUnhealthy(parent_.healthCheckerType(), host_, type);
      }
    } else {
      changed_state = HealthTransition::ChangePending;
    }
  }

  changed_state = clearPendingFlag(changed_state);

  if ((first_check_ || parent_.always_log_health_check_failures_) && parent_.event_logger_) {
    parent_.event_logger_->logUnhealthy(parent_.healthCheckerType(), host_, type, first_check_);
  }

  parent_.stats_.failure_.inc();
  if (type == envoy::data::core::v3::NETWORK) {
    parent_.stats_.network_failure_.inc();
  } else if (type == envoy::data::core::v3::PASSIVE) {
    parent_.stats_.passive_failure_.inc();
  }

  first_check_ = false;
  parent_.runCallbacks(host_, changed_state);
  return changed_state;
}

void HealthCheckerImplBase::ActiveHealthCheckSession::handleFailure(
    envoy::data::core::v3::HealthCheckFailureType type) {
  HealthTransition changed_state = setUnhealthy(type);
  // It's possible that the previous call caused this session to be deferred deleted.
  if (timeout_timer_ != nullptr) {
    timeout_timer_->disableTimer();
  }

  if (interval_timer_ != nullptr) {
    interval_timer_->enableTimer(parent_.interval(HealthState::Unhealthy, changed_state));
  }
}

HealthTransition
HealthCheckerImplBase::ActiveHealthCheckSession::clearPendingFlag(HealthTransition changed_state) {
  if (host_->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC)) {
    host_->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    // Even though the health value of the host might have not changed, we set this to Changed to
    // that the cluster can update its list of excluded hosts.
    return HealthTransition::Changed;
  }

  return changed_state;
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onIntervalBase() {
  onInterval();
  timeout_timer_->enableTimer(parent_.timeout_);
  parent_.stats_.attempt_.inc();
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onTimeoutBase() {
  onTimeout();
  handleFailure(envoy::data::core::v3::NETWORK);
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onInitialInterval() {
  if (parent_.initial_jitter_.count() == 0) {
    onIntervalBase();
  } else {
    interval_timer_->enableTimer(
        std::chrono::milliseconds(parent_.intervalWithJitter(0, parent_.initial_jitter_)));
  }
}

void HealthCheckEventLoggerImpl::logEjectUnhealthy(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host,
    envoy::data::core::v3::HealthCheckFailureType failure_type) {
  createHealthCheckEvent(health_checker_type, *host, [&failure_type](auto& event) {
    event.mutable_eject_unhealthy_event()->set_failure_type(failure_type);
  });
}

void HealthCheckEventLoggerImpl::logUnhealthy(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host,
    envoy::data::core::v3::HealthCheckFailureType failure_type, bool first_check) {
  createHealthCheckEvent(health_checker_type, *host, [&first_check, &failure_type](auto& event) {
    event.mutable_health_check_failure_event()->set_failure_type(failure_type);
    event.mutable_health_check_failure_event()->set_first_check(first_check);
  });
}

void HealthCheckEventLoggerImpl::logAddHealthy(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host, bool first_check) {
  createHealthCheckEvent(health_checker_type, *host, [&first_check](auto& event) {
    event.mutable_add_healthy_event()->set_first_check(first_check);
  });
}

void HealthCheckEventLoggerImpl::logDegraded(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host) {
  createHealthCheckEvent(health_checker_type, *host,
                         [](auto& event) { event.mutable_degraded_healthy_host(); });
}

void HealthCheckEventLoggerImpl::logNoLongerDegraded(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host) {
  createHealthCheckEvent(health_checker_type, *host,
                         [](auto& event) { event.mutable_no_longer_degraded_host(); });
}

void HealthCheckEventLoggerImpl::createHealthCheckEvent(
    envoy::data::core::v3::HealthCheckerType health_checker_type, const HostDescription& host,
    std::function<void(envoy::data::core::v3::HealthCheckEvent&)> callback) const {
  envoy::data::core::v3::HealthCheckEvent event;
  event.set_cluster_name(host.cluster().name());
  event.set_health_checker_type(health_checker_type);

  envoy::config::core::v3::Address address;
  Network::Utility::addressToProtobufAddress(*host.address(), address);
  *event.mutable_host() = std::move(address);

  TimestampUtil::systemClockToTimestamp(time_source_.systemTime(), *event.mutable_timestamp());

  callback(event);

  // Make sure the type enums make it into the JSON
  const auto json = MessageUtil::getJsonStringFromMessage(event, /* pretty_print */ false,
                                                          /* always_print_primitive_fields */ true);
  file_->write(fmt::format("{}\n", json));
}
} // namespace Upstream
} // namespace Envoy
