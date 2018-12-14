#include "common/upstream/outlier_detection_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {
namespace Outlier {

DetectorSharedPtr DetectorImplFactory::createForCluster(
    Cluster& cluster, const envoy::api::v2::Cluster& cluster_config, Event::Dispatcher& dispatcher,
    Runtime::Loader& runtime, EventLoggerSharedPtr event_logger) {
  if (cluster_config.has_outlier_detection()) {
    return DetectorImpl::create(cluster, cluster_config.outlier_detection(), dispatcher, runtime,
                                dispatcher.timeSystem(), std::move(event_logger));
  } else {
    return nullptr;
  }
}

void DetectorHostMonitorImpl::eject(MonotonicTime ejection_time) {
  ASSERT(!host_.lock()->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  host_.lock()->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  num_ejections_++;
  last_ejection_time_ = ejection_time;
}

void DetectorHostMonitorImpl::uneject(MonotonicTime unejection_time) {
  last_unejection_time_ = (unejection_time);
}

void DetectorHostMonitorImpl::updateCurrentSuccessRateBucket() {
  getSRMonitor<externalOrigin>()->updateCurrentSuccessRateBucket();
  getSRMonitor<localOrigin>()->updateCurrentSuccessRateBucket();
}

void DetectorHostMonitorImpl::putHttpResponseCode(uint64_t response_code) {
  getSRMonitor<externalOrigin>()->incTotalReqCounter();
  if (Http::CodeUtility::is5xx(response_code)) {
    std::shared_ptr<DetectorImpl> detector = detector_.lock();
    if (!detector) {
      // It's possible for the cluster/detector to go away while we still have a host in use.
      return;
    }
    if (Http::CodeUtility::isGatewayError(response_code)) {
      if (++consecutive_gateway_failure_ == detector->runtime().snapshot().getInteger(
                                                "outlier_detection.consecutive_gateway_failure",
                                                detector->config().consecutiveGatewayFailure())) {
        detector->onConsecutiveGatewayFailure(host_.lock());
      }
    } else {
      consecutive_gateway_failure_ = 0;
    }

    if (++consecutive_5xx_ ==
        detector->runtime().snapshot().getInteger("outlier_detection.consecutive_5xx",
                                                  detector->config().consecutive5xx())) {
      detector->onConsecutive5xx(host_.lock());
    }
  } else {
    getSRMonitor<externalOrigin>()->incSuccessReqCounter();
    consecutive_5xx_ = 0;
    consecutive_gateway_failure_ = 0;
  }
}

/* putResult is used to report events via enums, not http codes.
 */
void DetectorHostMonitorImpl::putResult(Result result) {
  switch (result) {
  // SUCCESS is used to report success for connection level. Server may still respond with
  // error, but connection to server was OK.
  case Result::SUCCESS:
    return localOriginNoFailure();
  // Connectivity releated errors.
  case Result::TIMEOUT:
  case Result::CONNECT_FAILED:
    return localOriginFailure();
  // REQUEST_FAILED is used when connection to server was successful, but transaction on server
  // level failed. Since it it similar to HTTP 5xx, map it to 5xx handler.
  case Result::REQUEST_FAILED:
    // map it to http code and call http handler.
    return putHttpResponseCode(500);
    break;
  // REQUEST_SUCCESS is used to report that transaction with non-http server was completed
  // successfully. This means that connection and server level transactions were successful. Map it
  // to http code 200 OK and indicate that there was no errors on connection level.
  case Result::REQUEST_SUCCESS:
    putHttpResponseCode(200);
    localOriginNoFailure();
    break;
  }
}

void DetectorHostMonitorImpl::localOriginFailure() {
  std::shared_ptr<DetectorImpl> detector = detector_.lock();
  if (!detector) {
    // It's possible for the cluster/detector to go away while we still have a host in use.
    return;
  }
  getSRMonitor<localOrigin>()->incTotalReqCounter();
  if (++consecutive_local_origin_failure_ ==
      detector->runtime().snapshot().getInteger(
          "outlier_detection.consecutive_local_origin_failure",
          detector->config().consecutiveLocalOriginFailure())) {
    detector->onConsecutiveLocalOriginFailure(host_.lock());
  }
}

void DetectorHostMonitorImpl::localOriginNoFailure() {
  std::shared_ptr<DetectorImpl> detector = detector_.lock();
  if (!detector) {
    // It's possible for the cluster/detector to go away while we still have a host in use.
    return;
  }

  getSRMonitor<localOrigin>()->incTotalReqCounter();
  getSRMonitor<localOrigin>()->incSuccessReqCounter();

  resetConsecutiveLocalOriginFailure();
}

DetectorConfig::DetectorConfig(const envoy::api::v2::cluster::OutlierDetection& config)
    : interval_ms_(static_cast<uint64_t>(PROTOBUF_GET_MS_OR_DEFAULT(config, interval, 10000))),
      base_ejection_time_ms_(
          static_cast<uint64_t>(PROTOBUF_GET_MS_OR_DEFAULT(config, base_ejection_time, 30000))),
      consecutive_5xx_(
          static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, consecutive_5xx, 5))),
      consecutive_gateway_failure_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, consecutive_gateway_failure, 5))),
      max_ejection_percent_(
          static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_ejection_percent, 10))),
      success_rate_minimum_hosts_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, success_rate_minimum_hosts, 5))),
      success_rate_request_volume_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, success_rate_request_volume, 100))),
      success_rate_stdev_factor_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, success_rate_stdev_factor, 1900))),
      enforcing_consecutive_5xx_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enforcing_consecutive_5xx, 100))),
      enforcing_consecutive_gateway_failure_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enforcing_consecutive_gateway_failure, 0))),
      enforcing_success_rate_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enforcing_success_rate, 100))),
      consecutive_local_origin_failure_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, consecutive_local_origin_failure, 5))),
      enforcing_consecutive_local_origin_failure_(
          static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
              config, enforcing_consecutive_local_origin_failure, 100))),
      enforcing_local_origin_success_rate_(static_cast<uint64_t>(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enforcing_local_origin_success_rate, 100))) {}

DetectorImpl::DetectorImpl(const Cluster& cluster,
                           const envoy::api::v2::cluster::OutlierDetection& config,
                           Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                           TimeSource& time_source, EventLoggerSharedPtr event_logger)
    : config_(config), dispatcher_(dispatcher), runtime_(runtime), time_source_(time_source),
      stats_(generateStats(cluster.info()->statsScope())),
      interval_timer_(dispatcher.createTimer([this]() -> void { onIntervalTimer(); })),
      event_logger_(event_logger) {
  // Insert success rate initial numbers for each type of SR detector
  success_rate_nums_[DetectorHostMonitor::externalOrigin] = std::make_tuple(-1, -1);
  success_rate_nums_[DetectorHostMonitor::localOrigin] = std::make_tuple(-1, -1);
}

DetectorImpl::~DetectorImpl() {
  for (auto host : host_monitors_) {
    if (host.first->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
      ASSERT(stats_.ejections_active_.value() > 0);
      stats_.ejections_active_.dec();
    }
  }
}

std::shared_ptr<DetectorImpl>
DetectorImpl::create(const Cluster& cluster,
                     const envoy::api::v2::cluster::OutlierDetection& config,
                     Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                     TimeSource& time_source, EventLoggerSharedPtr event_logger) {
  std::shared_ptr<DetectorImpl> detector(
      new DetectorImpl(cluster, config, dispatcher, runtime, time_source, event_logger));
  detector->initialize(cluster);

  return detector;
}

void DetectorImpl::initialize(const Cluster& cluster) {
  for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
    for (const HostSharedPtr& host : host_set->hosts()) {
      addHostMonitor(host);
    }
  }
  cluster.prioritySet().addMemberUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) -> void {
        for (const HostSharedPtr& host : hosts_added) {
          addHostMonitor(host);
        }

        for (const HostSharedPtr& host : hosts_removed) {
          ASSERT(host_monitors_.count(host) == 1);
          if (host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
            ASSERT(stats_.ejections_active_.value() > 0);
            stats_.ejections_active_.dec();
          }

          host_monitors_.erase(host);
        }
      });

  armIntervalTimer();
}

void DetectorImpl::addHostMonitor(HostSharedPtr host) {
  ASSERT(host_monitors_.count(host) == 0);
  DetectorHostMonitorImpl* monitor = new DetectorHostMonitorImpl(shared_from_this(), host);
  host_monitors_[host] = monitor;
  host->setOutlierDetector(DetectorHostMonitorPtr{monitor});
}

void DetectorImpl::armIntervalTimer() {
  interval_timer_->enableTimer(std::chrono::milliseconds(
      runtime_.snapshot().getInteger("outlier_detection.interval_ms", config_.intervalMs())));
}

void DetectorImpl::checkHostForUneject(HostSharedPtr host, DetectorHostMonitorImpl* monitor,
                                       MonotonicTime now) {
  if (!host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
    return;
  }

  std::chrono::milliseconds base_eject_time =
      std::chrono::milliseconds(runtime_.snapshot().getInteger(
          "outlier_detection.base_ejection_time_ms", config_.baseEjectionTimeMs()));
  ASSERT(monitor->numEjections() > 0);
  if ((base_eject_time * monitor->numEjections()) <= (now - monitor->lastEjectionTime().value())) {
    stats_.ejections_active_.dec();
    host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    // Reset the consecutive failure counters to avoid re-ejection on very few new errors due
    // to the non-triggering counter being close to its trigger value.
    host_monitors_[host]->resetConsecutive5xx();
    host_monitors_[host]->resetConsecutiveGatewayFailure();
    monitor->uneject(now);
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
  case EjectionType::ConsecutiveGatewayFailure:
    return runtime_.snapshot().featureEnabled(
        "outlier_detection.enforcing_consecutive_gateway_failure",
        config_.enforcingConsecutiveGatewayFailure());
  case EjectionType::SuccessRateExternalOrigin:
    return runtime_.snapshot().featureEnabled("outlier_detection.enforcing_success_rate",
                                              config_.enforcingSuccessRate());
  case EjectionType::ConsecutiveLocalOriginFailure:
    return runtime_.snapshot().featureEnabled(
        "outlier_detection.enforcing_consecutive_local_origin_failure",
        config_.enforcingConsecutiveLocalOriginFailure());
  case EjectionType::SuccessRateLocalOrigin:
    return runtime_.snapshot().featureEnabled(
        "outlier_detection.enforcing_local_origin_success_rate",
        config_.enforcingLocalOriginSuccessRate());
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void DetectorImpl::updateEnforcedEjectionStats(EjectionType type) {
  stats_.ejections_enforced_total_.inc();
  switch (type) {
  case EjectionType::SuccessRateExternalOrigin:
    stats_.ejections_enforced_success_rate_.inc();
    break;
  case EjectionType::Consecutive5xx:
    stats_.ejections_enforced_consecutive_5xx_.inc();
    break;
  case EjectionType::ConsecutiveGatewayFailure:
    stats_.ejections_enforced_consecutive_gateway_failure_.inc();
    break;
  case EjectionType::ConsecutiveLocalOriginFailure:
    stats_.ejections_enforced_consecutive_local_origin_failure_.inc();
    break;
  case EjectionType::SuccessRateLocalOrigin:
    stats_.ejections_enforced_local_origin_success_rate_.inc();
    break;
  }
}

void DetectorImpl::updateDetectedEjectionStats(EjectionType type) {
  switch (type) {
  case EjectionType::SuccessRateExternalOrigin:
    stats_.ejections_detected_success_rate_.inc();
    break;
  case EjectionType::Consecutive5xx:
    stats_.ejections_detected_consecutive_5xx_.inc();
    break;
  case EjectionType::ConsecutiveGatewayFailure:
    stats_.ejections_detected_consecutive_gateway_failure_.inc();
    break;
  case EjectionType::ConsecutiveLocalOriginFailure:
    stats_.ejections_detected_consecutive_local_origin_failure_.inc();
    break;
  case EjectionType::SuccessRateLocalOrigin:
    stats_.ejections_detected_local_origin_success_rate_.inc();
    break;
  }
}

void DetectorImpl::ejectHost(HostSharedPtr host, EjectionType type) {
  uint64_t max_ejection_percent = std::min<uint64_t>(
      100, runtime_.snapshot().getInteger("outlier_detection.max_ejection_percent",
                                          config_.maxEjectionPercent()));
  double ejected_percent = 100.0 * stats_.ejections_active_.value() / host_monitors_.size();
  // Note this is not currently checked per-priority level, so it is possible
  // for outlier detection to eject all hosts at any given priority level.
  if (ejected_percent < max_ejection_percent) {
    if (type == EjectionType::Consecutive5xx || type == EjectionType::SuccessRateExternalOrigin) {
      // Deprecated counter, preserving old behaviour until it's removed.
      stats_.ejections_total_.inc();
    }
    if (enforceEjection(type)) {
      stats_.ejections_active_.inc();
      updateEnforcedEjectionStats(type);
      host_monitors_[host]->eject(time_source_.monotonicTime());
      runCallbacks(host);

      if (event_logger_) {
        event_logger_->logEject(host, *this, type, true);
      }
    } else {
      if (event_logger_) {
        event_logger_->logEject(host, *this, type, false);
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

void DetectorImpl::notifyMainThreadConsecutiveError(HostSharedPtr host, EjectionType type) {
  // This event will come from all threads, so we synchronize with a post to the main thread.
  // NOTE: Unfortunately consecutive errors are complicated from a threading perspective because
  //       we catch consecutive errors on worker threads and then post back to the main thread.
  //       Clusters can get removed, and this means there is a race condition with this
  //       reverse post. The way we handle this is as follows:
  //       1) The only strong pointer to the detector is owned by the cluster.
  //       2) We post a weak pointer to the main thread.
  //       3) If when running on the main thread the weak pointer can be converted to a strong
  //          pointer, the detector/cluster must still exist so we can safely fire callbacks.
  //          Otherwise we do nothing since the detector/cluster is already gone.
  std::weak_ptr<DetectorImpl> weak_this = shared_from_this();
  dispatcher_.post([weak_this, host, type]() -> void {
    std::shared_ptr<DetectorImpl> shared_this = weak_this.lock();
    if (shared_this) {
      shared_this->onConsecutiveErrorWorker(host, type);
    }
  });
}

void DetectorImpl::onConsecutive5xx(HostSharedPtr host) {
  notifyMainThreadConsecutiveError(host, EjectionType::Consecutive5xx);
}

void DetectorImpl::onConsecutiveGatewayFailure(HostSharedPtr host) {
  notifyMainThreadConsecutiveError(host, EjectionType::ConsecutiveGatewayFailure);
}

void DetectorImpl::onConsecutiveLocalOriginFailure(HostSharedPtr host) {
  notifyMainThreadConsecutiveError(host, EjectionType::ConsecutiveLocalOriginFailure);
}

void DetectorImpl::onConsecutiveErrorWorker(HostSharedPtr host, EjectionType type) {
  // Ejections come in cross thread. There is a chance that the host has already been removed from
  // the set. If so, just ignore it.
  if (host_monitors_.count(host) == 0) {
    return;
  }
  if (host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
    return;
  }

  // We also reset the appropriate counter here to allow the monitor to detect a bout of consecutive
  // error responses even if the monitor is not charged with an interleaved non-error code.
  updateDetectedEjectionStats(type);
  ejectHost(host, type);

  // reset counters
  switch (type) {
  case EjectionType::Consecutive5xx:
    stats_.ejections_consecutive_5xx_.inc(); // Deprecated
    host_monitors_[host]->resetConsecutive5xx();
    break;
  case EjectionType::ConsecutiveGatewayFailure:
    host_monitors_[host]->resetConsecutiveGatewayFailure();
    break;
  case EjectionType::ConsecutiveLocalOriginFailure:
    host_monitors_[host]->resetConsecutiveLocalOriginFailure();
    break;
  case EjectionType::SuccessRateExternalOrigin:
  case EjectionType::SuccessRateLocalOrigin:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

std::tuple<double, double> DetectorImpl::successRateEjectionThreshold(
    double success_rate_sum, const std::vector<HostSuccessRatePair>& valid_success_rate_hosts,
    double success_rate_stdev_factor) {
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

  return std::make_tuple(mean, (mean - (success_rate_stdev_factor * stdev)));
}

void DetectorImpl::processSuccessRateEjections(
    DetectorHostMonitor::SuccessRateMonitorType monitor_type) {
  uint64_t success_rate_minimum_hosts = runtime_.snapshot().getInteger(
      "outlier_detection.success_rate_minimum_hosts", config_.successRateMinimumHosts());
  uint64_t success_rate_request_volume = runtime_.snapshot().getInteger(
      "outlier_detection.success_rate_request_volume", config_.successRateRequestVolume());
  std::vector<HostSuccessRatePair> valid_success_rate_hosts;
  double success_rate_sum = 0;

  // Reset the Detector's success rate mean and stdev.
  success_rate_nums_[monitor_type] = std::make_tuple(-1, -1);

  // Exit early if there are not enough hosts.
  if (host_monitors_.size() < success_rate_minimum_hosts) {
    return;
  }

  // reserve upper bound of vector size to avoid reallocation.
  valid_success_rate_hosts.reserve(host_monitors_.size());

  for (const auto& host : host_monitors_) {
    // Don't do work if the host is already ejected.
    if (!host.first->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
      absl::optional<double> host_success_rate = host.second->getSRMonitor(monitor_type)
                                                     ->successRateAccumulator()
                                                     .getSuccessRate(success_rate_request_volume);

      if (host_success_rate) {
        valid_success_rate_hosts.emplace_back(
            HostSuccessRatePair(host.first, host_success_rate.value()));
        success_rate_sum += host_success_rate.value();
        host.second->successRate(monitor_type, host_success_rate.value());
      }
    }
  }

  if (!valid_success_rate_hosts.empty() &&
      valid_success_rate_hosts.size() >= success_rate_minimum_hosts) {
    double success_rate_stdev_factor =
        runtime_.snapshot().getInteger("outlier_detection.success_rate_stdev_factor",
                                       config_.successRateStdevFactor()) /
        1000.0;
    success_rate_nums_[monitor_type] = successRateEjectionThreshold(
        success_rate_sum, valid_success_rate_hosts, success_rate_stdev_factor);
    double success_rate_ejection_threshold = std::get<1>(success_rate_nums_[monitor_type]);
    for (const auto& host_success_rate_pair : valid_success_rate_hosts) {
      if (host_success_rate_pair.success_rate_ < success_rate_ejection_threshold) {
        stats_.ejections_success_rate_.inc(); // Deprecated.
        EjectionType type = host_monitors_[host_success_rate_pair.host_]
                                ->getSRMonitor(monitor_type)
                                ->getEjectionType();
        updateDetectedEjectionStats(type);
        ejectHost(host_success_rate_pair.host_, type);
      }
    }
  }
}

void DetectorImpl::onIntervalTimer() {
  MonotonicTime now = time_source_.monotonicTime();

  for (auto host : host_monitors_) {
    checkHostForUneject(host.first, host.second, now);

    // Need to update the writer bucket to keep the data valid.
    host.second->updateCurrentSuccessRateBucket();
    // Refresh host success rate stat for the /clusters endpoint. If there is a new valid value, it
    // will get updated in processSuccessRateEjections().
    host.second->successRate(DetectorHostMonitor::localOrigin, -1);
    host.second->successRate(DetectorHostMonitor::externalOrigin, -1);
  }

  processSuccessRateEjections(DetectorHostMonitor::externalOrigin);
  processSuccessRateEjections(DetectorHostMonitor::localOrigin);

  armIntervalTimer();
}

void DetectorImpl::runCallbacks(HostSharedPtr host) {
  for (const ChangeStateCb& cb : callbacks_) {
    cb(host);
  }
}

void EventLoggerImpl::logEject(HostDescriptionConstSharedPtr host, Detector& detector,
                               EjectionType type, bool enforced) {
  // TODO(mattklein123): Log friendly host name (e.g., instance ID or DNS name).
  // clang-format off
  static const std::string json_5xx =
    std::string("{{") +
    "\"time\": \"{}\", " +
    "\"secs_since_last_action\": \"{}\", " +
    "\"cluster\": \"{}\", " +
    "\"upstream_url\": \"{}\", " +
    "\"action\": \"eject\", " +
    "\"type\": \"{}\", " +
    "\"num_ejections\": {}, " +
    "\"enforced\": \"{}\"" +
    "}}\n";

  static const std::string json_success_rate =
    std::string("{{") +
    "\"time\": \"{}\", " +
    "\"secs_since_last_action\": \"{}\", " +
    "\"cluster\": \"{}\", " +
    "\"upstream_url\": \"{}\", " +
    "\"action\": \"eject\", " +
    "\"type\": \"{}\", " +
    "\"num_ejections\": {}, " +
    "\"enforced\": \"{}\", " +
    "\"host_success_rate\": \"{}\", " +
    "\"cluster_average_success_rate\": \"{}\", " +
    "\"cluster_success_rate_ejection_threshold\": \"{}\"" +
    "}}\n";
  // clang-format on
  SystemTime now = time_source_.systemTime();
  MonotonicTime monotonic_now = time_source_.monotonicTime();

  switch (type) {
  case EjectionType::Consecutive5xx:
  case EjectionType::ConsecutiveGatewayFailure:
  case EjectionType::ConsecutiveLocalOriginFailure:
    file_->write(fmt::format(
        json_5xx, AccessLogDateTimeFormatter::fromTime(now),
        secsSinceLastAction(host->outlierDetector().lastUnejectionTime(), monotonic_now),
        host->cluster().name(), host->address()->asString(), typeToString(type),
        host->outlierDetector().numEjections(), enforced));
    break;
  case EjectionType::SuccessRateExternalOrigin:
  case EjectionType::SuccessRateLocalOrigin:
    file_->write(fmt::format(
        json_success_rate, AccessLogDateTimeFormatter::fromTime(now),
        secsSinceLastAction(host->outlierDetector().lastUnejectionTime(), monotonic_now),
        host->cluster().name(), host->address()->asString(), typeToString(type),
        host->outlierDetector().numEjections(), enforced,
        host->outlierDetector().successRate(DetectorHostMonitor::externalOrigin),
        detector.successRateAverage(DetectorHostMonitor::externalOrigin),
        detector.successRateEjectionThreshold(DetectorHostMonitor::externalOrigin)));
    break;
  }
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
  SystemTime now = time_source_.systemTime();
  MonotonicTime monotonic_now = time_source_.monotonicTime();
  file_->write(fmt::format(
      json, AccessLogDateTimeFormatter::fromTime(now),
      secsSinceLastAction(host->outlierDetector().lastEjectionTime(), monotonic_now),
      host->cluster().name(), host->address()->asString(), host->outlierDetector().numEjections()));
}

std::string EventLoggerImpl::typeToString(EjectionType type) {
  switch (type) {
  case EjectionType::Consecutive5xx:
    return "5xx";
  case EjectionType::ConsecutiveGatewayFailure:
    return "GatewayFailure";
  case EjectionType::SuccessRateExternalOrigin:
    return "SuccessRate-ExternalOrigin";
  case EjectionType::SuccessRateLocalOrigin:
    return "SuccessRate-LocalOrigin";
  case EjectionType::ConsecutiveLocalOriginFailure:
    return "LocalOriginFailure";
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

int EventLoggerImpl::secsSinceLastAction(const absl::optional<MonotonicTime>& lastActionTime,
                                         MonotonicTime now) {
  if (lastActionTime) {
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

absl::optional<double>
SuccessRateAccumulator::getSuccessRate(uint64_t success_rate_request_volume) {
  if (backup_success_rate_bucket_->total_request_counter_ < success_rate_request_volume) {
    return absl::optional<double>();
  }

  return absl::optional<double>(backup_success_rate_bucket_->success_request_counter_ * 100.0 /
                                backup_success_rate_bucket_->total_request_counter_);
}

} // namespace Outlier
} // namespace Upstream
} // namespace Envoy
