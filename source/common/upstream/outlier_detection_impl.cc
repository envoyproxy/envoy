#include "outlier_detection_impl.h"

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/codes.h"

namespace Upstream {
namespace Outlier {

DetectorPtr DetectorImplFactory::createForCluster(Cluster& cluster,
                                                  const Json::Object& cluster_config,
                                                  Event::Dispatcher& dispatcher,
                                                  Runtime::Loader& runtime,
                                                  EventLoggerPtr event_logger) {
  if (cluster_config.hasObject("outlier_detection")) {
    return DetectorImpl::create(cluster, *cluster_config.getObject("outlier_detection"), dispatcher,
                                runtime, ProdSystemTimeSource::instance_, event_logger);
  } else {
    return nullptr;
  }
}

void DetectorHostSinkImpl::eject(SystemTime ejection_time) {
  ASSERT(!host_.lock()->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  host_.lock()->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  num_ejections_++;
  last_ejection_time_.value(ejection_time);
}

void DetectorHostSinkImpl::uneject(SystemTime unejection_time) {
  last_unejection_time_.value(unejection_time);
}

void DetectorHostSinkImpl::updateCurrentSRBucket() {
  sr_accumulator_bucket_.store(sr_accumulator_.getCurrentWriter());
}

void DetectorHostSinkImpl::putHttpResponseCode(uint64_t response_code) {
  sr_accumulator_bucket_.load()->total_rq_counter_++;
  if (Http::CodeUtility::is5xx(response_code)) {
    std::shared_ptr<DetectorImpl> detector = detector_.lock();
    if (!detector) {
      // It's possible for the cluster/detector to go away while we still have a host in use.
      return;
    }

    if (++consecutive_5xx_ ==
        detector->runtime().snapshot().getInteger("outlier_detection.consecutive_5xx",
                                                  detector->config().consecutive5xx())) {
      detector->onConsecutive5xx(host_.lock());
    }
  } else {
    sr_accumulator_bucket_.load()->success_rq_counter_++;
    consecutive_5xx_ = 0;
  }
}

DetectorConfig::DetectorConfig(const Json::Object& json_config)
    : interval_ms_(static_cast<uint64_t>(json_config.getInteger("interval_ms", 10000))),
      base_ejection_time_ms_(
          static_cast<uint64_t>(json_config.getInteger("base_ejection_time_ms", 30000))),
      consecutive_5xx_(static_cast<uint64_t>(json_config.getInteger("consecutive_5xx", 5))),
      max_ejection_percent_(
          static_cast<uint64_t>(json_config.getInteger("max_ejection_percent", 10))),
      significant_host_threshold_(
          static_cast<uint64_t>(json_config.getInteger("significant_host_threshold", 5))),
      rq_volume_threshold_(
          static_cast<uint64_t>(json_config.getInteger("rq_volume_threshold", 100))),
      enforcing_consecutive_5xx_(
          static_cast<uint64_t>(json_config.getInteger("enforcing_consecutive_5xx", 100))),
      enforcing_sr_(static_cast<uint64_t>(json_config.getInteger("enforcing_sr", 100))) {}

DetectorImpl::DetectorImpl(const Cluster& cluster, const Json::Object& json_config,
                           Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                           SystemTimeSource& time_source, EventLoggerPtr event_logger)
    : config_(json_config), dispatcher_(dispatcher), runtime_(runtime), time_source_(time_source),
      stats_(generateStats(cluster.info()->statsScope())),
      interval_timer_(dispatcher.createTimer([this]() -> void { onIntervalTimer(); })),
      event_logger_(event_logger) {}

DetectorImpl::~DetectorImpl() {
  for (auto host : host_sinks_) {
    if (host.first->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
      ASSERT(stats_.ejections_active_.value() > 0);
      stats_.ejections_active_.dec();
    }
  }
}

std::shared_ptr<DetectorImpl>
DetectorImpl::create(const Cluster& cluster, const Json::Object& json_config,
                     Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                     SystemTimeSource& time_source, EventLoggerPtr event_logger) {
  std::shared_ptr<DetectorImpl> detector(
      new DetectorImpl(cluster, json_config, dispatcher, runtime, time_source, event_logger));
  detector->initialize(cluster);
  return detector;
}

void DetectorImpl::initialize(const Cluster& cluster) {
  for (HostPtr host : cluster.hosts()) {
    addHostSink(host);
  }

  cluster.addMemberUpdateCb([this](const std::vector<HostPtr>& hosts_added,
                                   const std::vector<HostPtr>& hosts_removed) -> void {
    for (HostPtr host : hosts_added) {
      addHostSink(host);
    }

    for (HostPtr host : hosts_removed) {
      ASSERT(host_sinks_.count(host) == 1);
      if (host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
        ASSERT(stats_.ejections_active_.value() > 0);
        stats_.ejections_active_.dec();
      }

      host_sinks_.erase(host);
    }
  });

  armIntervalTimer();
}

void DetectorImpl::addHostSink(HostPtr host) {
  ASSERT(host_sinks_.count(host) == 0);
  DetectorHostSinkImpl* sink = new DetectorHostSinkImpl(shared_from_this(), host);
  host_sinks_[host] = sink;
  host->setOutlierDetector(DetectorHostSinkPtr{sink});
}

void DetectorImpl::armIntervalTimer() {
  interval_timer_->enableTimer(std::chrono::milliseconds(
      runtime_.snapshot().getInteger("outlier_detection.interval_ms", config_.intervalMs())));
}

void DetectorImpl::checkHostForUneject(HostPtr host, DetectorHostSinkImpl* sink, SystemTime now) {
  if (!host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
    return;
  }

  std::chrono::milliseconds base_eject_time =
      std::chrono::milliseconds(runtime_.snapshot().getInteger(
          "outlier_detection.base_ejection_time_ms", config_.baseEjectionTimeMs()));
  ASSERT(sink->numEjections() > 0)
  if ((base_eject_time * sink->numEjections()) <= (now - sink->lastEjectionTime().value())) {
    stats_.ejections_active_.dec();
    host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    sink->uneject(now);
    runCallbacks(host);

    if (event_logger_) {
      event_logger_->logUneject(host);
    }
  }
}

bool DetectorImpl::enforceEjection(EjectionType type) {
  switch (type) {
  case EjectionType::Consecutive5xx:
    return runtime_.snapshot().featureEnabled("outlier_detection.enforcing_consecutive_5xx",
                                              config_.enforcingConsecutive5xx());
  case EjectionType::SuccessRate:
    return runtime_.snapshot().featureEnabled("outlier_detection.enforcing_sr",
                                              config_.enforcingSR());
  }

  NOT_IMPLEMENTED;
}

void DetectorImpl::ejectHost(HostPtr host, EjectionType type) {
  uint64_t max_ejection_percent = std::min<uint64_t>(
      100, runtime_.snapshot().getInteger("outlier_detection.max_ejection_percent",
                                          config_.maxEjectionPercent()));
  double ejected_percent = 100.0 * stats_.ejections_active_.value() / host_sinks_.size();
  if (ejected_percent < max_ejection_percent) {
    stats_.ejections_total_.inc();
    if (enforceEjection(type)) {
      stats_.ejections_active_.inc();
      host_sinks_[host]->eject(time_source_.currentSystemTime());
      runCallbacks(host);

      if (event_logger_) {
        event_logger_->logEject(host, type);
      }
    }
  } else {
    stats_.ejections_overflow_.inc();
  }
}

DetectionStats DetectorImpl::generateStats(Stats::Scope& scope) {
  std::string prefix("outlier_detection.");
  return {ALL_OUTLIER_DETECTION_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                      POOL_GAUGE_PREFIX(scope, prefix))};
}

void DetectorImpl::onConsecutive5xx(HostPtr host) {
  // This event will come from all threads, so we synchronize with a post to the main thread.
  // NOTE: Unfortunately consecutive 5xx is complicated from a threading perspective because
  //       we catch consecutive 5xx on worker threads and then post back to the main thread.
  //       Clusters can get removed, and this means there is a race condition with this
  //       reverse post. The way we handle this is as follows:
  //       1) The only strong pointer to the detector is owned by the cluster.
  //       2) We post a weak pointer to the main thread.
  //       3) If when running on the main thread the weak pointer can be converted to a strong
  //          pointer, the detector/cluster must still exist so we can safely fire callbacks.
  //          Otherwise we do nothing since the detector/cluster is already gone.
  std::weak_ptr<DetectorImpl> weak_this = shared_from_this();
  dispatcher_.post([weak_this, host]() -> void {
    std::shared_ptr<DetectorImpl> shared_this = weak_this.lock();
    if (shared_this) {
      shared_this->onConsecutive5xxWorker(host);
    }
  });
}

void DetectorImpl::onConsecutive5xxWorker(HostPtr host) {
  // This comes in cross thread. There is a chance that the host has already been removed from
  // the set. If so, just ignore it.
  if (host_sinks_.count(host) == 0) {
    return;
  }

  if (host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
    return;
  }

  stats_.ejections_consecutive_5xx_.inc();
  ejectHost(host, EjectionType::Consecutive5xx);
}

double DetectorImpl::srEjectionThreshold(double sr_sum, std::vector<double>& sr_data) {
  double mean = sr_sum / sr_data.size();
  double stdev = 0;
  std::for_each(sr_data.begin(), sr_data.end(),
                [&stdev, mean](double& v) { stdev += std::pow(v - mean, 2); });
  stdev /= sr_data.size();
  stdev = std::sqrt(stdev);

  return mean - (sr_stdev_factor_ * stdev);
}

void DetectorImpl::onIntervalTimer() {
  SystemTime now = time_source_.currentSystemTime();
  std::unordered_map<HostPtr, double> valid_sr_hosts;
  std::vector<double> sr_data;
  double sr_sum = 0;
  for (auto host : host_sinks_) {
    checkHostForUneject(host.first, host.second, now);

    // Success Rate Outlier Detection
    // First swap out the current bucket been written to, to keep data valid
    host.second->updateCurrentSRBucket();

    // If there are not enough hosts to begin with, don't do the work.
    if (host_sinks_.size() >=
        runtime_.snapshot().getInteger("outlier_detection.significant_host_threshold",
                                       config_.significantHostThreshold())) {
      Optional<double> host_sr = host.second->srAccumulator().getSR(runtime_.snapshot().getInteger(
          "outlier_detection.rq_volume_threshold", config_.rqVolumeThreshold()));

      if (host_sr.valid()) {
        valid_sr_hosts[host.first] = host_sr.value();
        sr_data.emplace_back(host_sr.value());
        sr_sum += host_sr.value();
      }
    }
  }

  if (valid_sr_hosts.size() >=
      runtime_.snapshot().getInteger("outlier_detection.significant_host_threshold",
                                     config_.significantHostThreshold())) {
    double ejection_threshold = srEjectionThreshold(sr_sum, sr_data);
    for (auto host : valid_sr_hosts) {
      if (host.second < ejection_threshold) {
        stats_.ejections_sr_.inc();
        ejectHost(host.first, EjectionType::SuccessRate);
      }
    }
  }

  armIntervalTimer();
}

void DetectorImpl::runCallbacks(HostPtr host) {
  for (ChangeStateCb cb : callbacks_) {
    cb(host);
  }
}

void EventLoggerImpl::logEject(HostDescriptionPtr host, EjectionType type) {
  // TODO(mattklein123): Log friendly host name (e.g., instance ID or DNS name).
  // clang-format off
  static const std::string json =
    std::string("{{") +
    "\"time\": \"{}\", " +
    "\"secs_since_last_action\": \"{}\", " +
    "\"cluster\": \"{}\", " +
    "\"upstream_url\": \"{}\", " +
    "\"action\": \"eject\", " +
    "\"type\": \"{}\", " +
    "\"num_ejections\": {}" +
    "}}\n";
  // clang-format on
  SystemTime now = time_source_.currentSystemTime();
  file_->write(fmt::format(json, AccessLogDateTimeFormatter::fromTime(now),
                           secsSinceLastAction(host->outlierDetector().lastUnejectionTime(), now),
                           host->cluster().name(), host->address()->asString(), typeToString(type),
                           host->outlierDetector().numEjections()));
}

void EventLoggerImpl::logUneject(HostDescriptionPtr host) {
  // TODO(mattklein123): Log friendly host name (e.g., instance ID or DNS name).
  // clang-format off
  static const std::string json =
    std::string("{{") +
    "\"time\": \"{}\", " +
    "\"secs_since_last_action\": \"{}\", " +
    "\"cluster\": \"{}\", " +
    "\"upstream_url\": \"{}\", " +
    "\"action\": \"uneject\", " +
    "\"num_ejections\": {}" +
    "}}\n";
  // clang-format on
  SystemTime now = time_source_.currentSystemTime();
  file_->write(fmt::format(json, AccessLogDateTimeFormatter::fromTime(now),
                           secsSinceLastAction(host->outlierDetector().lastEjectionTime(), now),
                           host->cluster().name(), host->address()->asString(),
                           host->outlierDetector().numEjections()));
}

std::string EventLoggerImpl::typeToString(EjectionType type) {
  switch (type) {
  case EjectionType::Consecutive5xx:
    return "5xx";
  case EjectionType::SuccessRate:
    return "SR";
  }

  NOT_IMPLEMENTED;
}

int EventLoggerImpl::secsSinceLastAction(const Optional<SystemTime>& lastActionTime,
                                         SystemTime now) {
  if (lastActionTime.valid()) {
    return std::chrono::duration_cast<std::chrono::seconds>(now - lastActionTime.value()).count();
  }
  return -1;
}

SRAccumulatorBucket* SRAccumulatorImpl::getCurrentWriter() {
  // Right now current_ is being written to and backup_ is not. Flush the backup and swap.
  backup_sr_bucket_->success_rq_counter_ = 0;
  backup_sr_bucket_->total_rq_counter_ = 0;

  current_sr_bucket_.swap(backup_sr_bucket_);

  return current_sr_bucket_.get();
}

Optional<double> SRAccumulatorImpl::getSR(uint64_t rq_volume_threshold) {
  if (backup_sr_bucket_->total_rq_counter_ < rq_volume_threshold) {
    return Optional<double>();
  }

  return Optional<double>(backup_sr_bucket_->success_rq_counter_ * 100 /
                          backup_sr_bucket_->total_rq_counter_);
}

} // Outlier
} // Upstream
