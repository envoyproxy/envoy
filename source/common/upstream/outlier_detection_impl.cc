#include "outlier_detection_impl.h"

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/codes.h"

namespace Upstream {
namespace Outlier {

DetectorSharedPtr DetectorImplFactory::createForCluster(Cluster& cluster,
                                                        const Json::Object& cluster_config,
                                                        Event::Dispatcher& dispatcher,
                                                        Runtime::Loader& runtime,
                                                        EventLoggerSharedPtr event_logger) {
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

void DetectorHostSinkImpl::updateCurrentSuccessRateBucket() {
  success_rate_accumulator_bucket_.store(success_rate_accumulator_.updateCurrentWriter());
}

void DetectorHostSinkImpl::putHttpResponseCode(uint64_t response_code) {
  success_rate_accumulator_bucket_.load()->total_request_counter_++;
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
    success_rate_accumulator_bucket_.load()->success_request_counter_++;
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
      success_rate_minimum_hosts_(
          static_cast<uint64_t>(json_config.getInteger("success_rate_minimum_hosts", 5))),
      success_rate_request_volume_(
          static_cast<uint64_t>(json_config.getInteger("success_rate_request_volume", 100))),
      enforcing_consecutive_5xx_(
          static_cast<uint64_t>(json_config.getInteger("enforcing_consecutive_5xx", 100))),
      enforcing_success_rate_(
          static_cast<uint64_t>(json_config.getInteger("enforcing_success_rate", 100))) {}

DetectorImpl::DetectorImpl(const Cluster& cluster, const Json::Object& json_config,
                           Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                           SystemTimeSource& time_source, EventLoggerSharedPtr event_logger)
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
                     SystemTimeSource& time_source, EventLoggerSharedPtr event_logger) {
  std::shared_ptr<DetectorImpl> detector(
      new DetectorImpl(cluster, json_config, dispatcher, runtime, time_source, event_logger));
  detector->initialize(cluster);
  return detector;
}

void DetectorImpl::initialize(const Cluster& cluster) {
  for (HostSharedPtr host : cluster.hosts()) {
    addHostSink(host);
  }

  cluster.addMemberUpdateCb([this](const std::vector<HostSharedPtr>& hosts_added,
                                   const std::vector<HostSharedPtr>& hosts_removed) -> void {
    for (HostSharedPtr host : hosts_added) {
      addHostSink(host);
    }

    for (HostSharedPtr host : hosts_removed) {
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

void DetectorImpl::addHostSink(HostSharedPtr host) {
  ASSERT(host_sinks_.count(host) == 0);
  DetectorHostSinkImpl* sink = new DetectorHostSinkImpl(shared_from_this(), host);
  host_sinks_[host] = sink;
  host->setOutlierDetector(DetectorHostSinkPtr{sink});
}

void DetectorImpl::armIntervalTimer() {
  interval_timer_->enableTimer(std::chrono::milliseconds(
      runtime_.snapshot().getInteger("outlier_detection.interval_ms", config_.intervalMs())));
}

void DetectorImpl::checkHostForUneject(HostSharedPtr host, DetectorHostSinkImpl* sink,
                                       SystemTime now) {
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
    return runtime_.snapshot().featureEnabled("outlier_detection.enforcing_success_rate",
                                              config_.enforcingSuccessRate());
  }

  NOT_REACHED;
}

void DetectorImpl::ejectHost(HostSharedPtr host, EjectionType type) {
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

void DetectorImpl::onConsecutive5xx(HostSharedPtr host) {
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

void DetectorImpl::onConsecutive5xxWorker(HostSharedPtr host) {
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

// The canonical factor for outlier detection in normal distributions is 2. However, host
// success rates are intuitively a distribution with negative skew, with most of the mass around
// 100 and a left tail. Therefore, a more aggressive (lower) factor is needed to detect
// outliers.
const double Utility::SUCCESS_RATE_STDEV_FACTOR = 1.9;

double Utility::successRateEjectionThreshold(
    double success_rate_sum, const std::vector<HostSuccessRatePair>& valid_success_rate_hosts) {
  // This function is using mean and standard deviation as statistical measures for outlier
  // detection. First the mean is calculated by dividing the sum of success rate data over the
  // number of data points. Then variance is calculated by taking the mean of the
  // squared difference of data points to the mean of the data. Then standard deviation is
  // calculated by taking the square root of the variance. Then the outlier threshold is
  // calculated as the difference between the mean and the product of the standard
  // deviation and a constant factor.
  //
  // For example with a data set that looks like success_rate_data = {50, 100, 100, 100, 100} the
  // math would work as follows:
  // success_rate_sum = 450
  // mean = 90
  // variance = 400
  // stdev = 20
  // threshold returned = 52
  double mean = success_rate_sum / valid_success_rate_hosts.size();
  double variance = 0;
  std::for_each(valid_success_rate_hosts.begin(), valid_success_rate_hosts.end(),
                [&variance, mean](HostSuccessRatePair v) {
                  variance += std::pow(v.success_rate_ - mean, 2);
                });
  variance /= valid_success_rate_hosts.size();
  double stdev = std::sqrt(variance);

  return mean - (SUCCESS_RATE_STDEV_FACTOR * stdev);
}

void DetectorImpl::processSuccessRateEjections() {
  uint64_t success_rate_minimum_hosts = runtime_.snapshot().getInteger(
      "outlier_detection.success_rate_minimum_hosts", config_.successRateMinimumHosts());
  uint64_t success_rate_request_volume = runtime_.snapshot().getInteger(
      "outlier_detection.success_rate_request_volume", config_.successRateRequestVolume());
  std::vector<HostSuccessRatePair> valid_success_rate_hosts;
  double success_rate_sum = 0;

  // Exit early if there are not enough hosts.
  if (host_sinks_.size() < success_rate_minimum_hosts) {
    return;
  }

  // reserve upper bound of vector size to avoid reallocation.
  valid_success_rate_hosts.reserve(host_sinks_.size());

  for (const auto& host : host_sinks_) {
    // Don't do work if the host is already ejected.
    if (!host.first->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
      Optional<double> host_success_rate =
          host.second->successRateAccumulator().getSuccessRate(success_rate_request_volume);

      if (host_success_rate.valid()) {
        valid_success_rate_hosts.emplace_back(
            HostSuccessRatePair(host.first, host_success_rate.value()));
        success_rate_sum += host_success_rate.value();
        host.first->successRate(host_success_rate.value());
      }
    }
  }

  if (valid_success_rate_hosts.size() >= success_rate_minimum_hosts) {
    double ejection_threshold =
        Utility::successRateEjectionThreshold(success_rate_sum, valid_success_rate_hosts);
    for (const auto& host_success_rate_pair : valid_success_rate_hosts) {
      if (host_success_rate_pair.success_rate_ < ejection_threshold) {
        stats_.ejections_success_rate_.inc();
        ejectHost(host_success_rate_pair.host_, EjectionType::SuccessRate);
      }
    }
  }
}

void DetectorImpl::onIntervalTimer() {
  SystemTime now = time_source_.currentSystemTime();

  for (auto host : host_sinks_) {
    checkHostForUneject(host.first, host.second, now);

    // Need to update the writer bucket to keep the data valid.
    host.second->updateCurrentSuccessRateBucket();
    // Refresh host success rate stat for the /clusters endpoint. If there is a new valid value it
    // will get updated in processSuccessRateEjections().
    host.first->successRate(-1);
  }

  processSuccessRateEjections();

  armIntervalTimer();
}

void DetectorImpl::runCallbacks(HostSharedPtr host) {
  for (ChangeStateCb cb : callbacks_) {
    cb(host);
  }
}

void EventLoggerImpl::logEject(HostDescriptionConstSharedPtr host, EjectionType type) {
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

void EventLoggerImpl::logUneject(HostDescriptionConstSharedPtr host) {
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
    return "SuccessRate";
  }

  NOT_REACHED;
}

int EventLoggerImpl::secsSinceLastAction(const Optional<SystemTime>& lastActionTime,
                                         SystemTime now) {
  if (lastActionTime.valid()) {
    return std::chrono::duration_cast<std::chrono::seconds>(now - lastActionTime.value()).count();
  }
  return -1;
}

SuccessRateAccumulatorBucket* SuccessRateAccumulator::updateCurrentWriter() {
  // Right now current is being written to and backup is not. Flush the backup and swap.
  backup_success_rate_bucket_->success_request_counter_ = 0;
  backup_success_rate_bucket_->total_request_counter_ = 0;

  current_success_rate_bucket_.swap(backup_success_rate_bucket_);

  return current_success_rate_bucket_.get();
}

Optional<double> SuccessRateAccumulator::getSuccessRate(uint64_t success_rate_request_volume) {
  if (backup_success_rate_bucket_->total_request_counter_ < success_rate_request_volume) {
    return Optional<double>();
  }

  return Optional<double>(backup_success_rate_bucket_->success_request_counter_ * 100.0 /
                          backup_success_rate_bucket_->total_request_counter_);
}

} // Outlier
} // Upstream
