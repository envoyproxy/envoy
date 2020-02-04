#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/common/time.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/outlier_detection.pb.h"
#include "envoy/data/cluster/v2alpha/outlier_detection_event.pb.h"
#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {
namespace Outlier {

/**
 * Null host monitor implementation.
 */
class DetectorHostMonitorNullImpl : public DetectorHostMonitor {
public:
  // Upstream::Outlier::DetectorHostMonitor
  uint32_t numEjections() override { return 0; }
  void putHttpResponseCode(uint64_t) override {}
  void putResult(Result, absl::optional<uint64_t>) override {}
  void putResponseTime(std::chrono::milliseconds) override {}
  const absl::optional<MonotonicTime>& lastEjectionTime() override { return time_; }
  const absl::optional<MonotonicTime>& lastUnejectionTime() override { return time_; }
  double successRate(SuccessRateMonitorType) const override { return -1; }

private:
  const absl::optional<MonotonicTime> time_;
};

/**
 * Factory for creating a detector from a proto configuration.
 */
class DetectorImplFactory {
public:
  static DetectorSharedPtr
  createForCluster(Cluster& cluster, const envoy::config::cluster::v3::Cluster& cluster_config,
                   Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                   EventLoggerSharedPtr event_logger);
};

/**
 * Thin struct to facilitate calculations for success rate outlier detection.
 */
struct HostSuccessRatePair {
  HostSuccessRatePair(HostSharedPtr host, double success_rate)
      : host_(host), success_rate_(success_rate) {}
  HostSharedPtr host_;
  double success_rate_;
};

struct SuccessRateAccumulatorBucket {
  std::atomic<uint64_t> success_request_counter_;
  std::atomic<uint64_t> total_request_counter_;
};

/**
 * The SuccessRateAccumulator uses the SuccessRateAccumulatorBucket to get per host success rate
 * stats. This implementation has a fixed window size of time, and thus only needs a
 * bucket to write to, and a bucket to accumulate/run stats over.
 */
class SuccessRateAccumulator {
public:
  SuccessRateAccumulator()
      : current_success_rate_bucket_(new SuccessRateAccumulatorBucket()),
        backup_success_rate_bucket_(new SuccessRateAccumulatorBucket()) {}

  /**
   * This function updates the bucket to write data to.
   * @return a pointer to the SuccessRateAccumulatorBucket.
   */
  SuccessRateAccumulatorBucket* updateCurrentWriter();
  /**
   * This function returns the success rate of a host over a window of time if the request volume is
   * high enough. The underlying window of time could be dynamically adjusted. In the current
   * implementation it is a fixed time window.
   * @param success_rate_request_volume the threshold of requests an accumulator has to have in
   *                                    order to be able to return a significant success rate value.
   * @return a valid absl::optional<double> with the success rate. If there were not enough
   * requests, an invalid absl::optional<double> is returned.
   */
  absl::optional<std::pair<double, uint64_t>> getSuccessRateAndVolume();

private:
  std::unique_ptr<SuccessRateAccumulatorBucket> current_success_rate_bucket_;
  std::unique_ptr<SuccessRateAccumulatorBucket> backup_success_rate_bucket_;
};

class SuccessRateMonitor {
public:
  SuccessRateMonitor(envoy::data::cluster::v2alpha::OutlierEjectionType ejection_type)
      : ejection_type_(ejection_type), success_rate_(-1) {
    // Point the success_rate_accumulator_bucket_ pointer to a bucket.
    updateCurrentSuccessRateBucket();
  }
  double getSuccessRate() const { return success_rate_; }
  SuccessRateAccumulator& successRateAccumulator() { return success_rate_accumulator_; }
  void setSuccessRate(double new_success_rate) { success_rate_ = new_success_rate; }
  void updateCurrentSuccessRateBucket() {
    success_rate_accumulator_bucket_.store(success_rate_accumulator_.updateCurrentWriter());
  }
  void incTotalReqCounter() { success_rate_accumulator_bucket_.load()->total_request_counter_++; }
  void incSuccessReqCounter() {
    success_rate_accumulator_bucket_.load()->success_request_counter_++;
  }

  envoy::data::cluster::v2alpha::OutlierEjectionType getEjectionType() const {
    return ejection_type_;
  }

private:
  SuccessRateAccumulator success_rate_accumulator_;
  std::atomic<SuccessRateAccumulatorBucket*> success_rate_accumulator_bucket_;
  envoy::data::cluster::v2alpha::OutlierEjectionType ejection_type_;
  double success_rate_;
};

class DetectorImpl;

/**
 * Implementation of DetectorHostMonitor for the generic detector.
 */
class DetectorHostMonitorImpl : public DetectorHostMonitor {
public:
  DetectorHostMonitorImpl(std::shared_ptr<DetectorImpl> detector, HostSharedPtr host);

  void eject(MonotonicTime ejection_time);
  void uneject(MonotonicTime ejection_time);

  void resetConsecutive5xx() { consecutive_5xx_ = 0; }
  void resetConsecutiveGatewayFailure() { consecutive_gateway_failure_ = 0; }
  void resetConsecutiveLocalOriginFailure() { consecutive_local_origin_failure_ = 0; }
  static absl::optional<Http::Code> resultToHttpCode(Result result);

  // Upstream::Outlier::DetectorHostMonitor
  uint32_t numEjections() override { return num_ejections_; }
  void putHttpResponseCode(uint64_t response_code) override;
  void putResult(Result result, absl::optional<uint64_t> code) override;
  void putResponseTime(std::chrono::milliseconds) override {}
  const absl::optional<MonotonicTime>& lastEjectionTime() override { return last_ejection_time_; }
  const absl::optional<MonotonicTime>& lastUnejectionTime() override {
    return last_unejection_time_;
  }

  const SuccessRateMonitor& getSRMonitor(SuccessRateMonitorType type) const {
    return (SuccessRateMonitorType::ExternalOrigin == type) ? external_origin_sr_monitor_
                                                            : local_origin_sr_monitor_;
  }

  SuccessRateMonitor& getSRMonitor(SuccessRateMonitorType type) {
    // Call const version of the same method
    return const_cast<SuccessRateMonitor&>(
        const_cast<const DetectorHostMonitorImpl*>(this)->getSRMonitor(type));
  }

  double successRate(SuccessRateMonitorType type) const override {
    return getSRMonitor(type).getSuccessRate();
  }
  void updateCurrentSuccessRateBucket();
  void successRate(SuccessRateMonitorType type, double new_success_rate) {
    getSRMonitor(type).setSuccessRate(new_success_rate);
  }

  // handlers for reporting local origin errors
  void localOriginFailure();
  void localOriginNoFailure();

private:
  std::weak_ptr<DetectorImpl> detector_;
  std::weak_ptr<Host> host_;
  absl::optional<MonotonicTime> last_ejection_time_;
  absl::optional<MonotonicTime> last_unejection_time_;
  uint32_t num_ejections_{};

  // counters for externally generated failures
  std::atomic<uint32_t> consecutive_5xx_{0};
  std::atomic<uint32_t> consecutive_gateway_failure_{0};

  // counters for local origin failures
  std::atomic<uint32_t> consecutive_local_origin_failure_{0};

  // success rate monitors:
  // - external_origin: for all events when external/local are not split
  //   and for external origin failures when external/local events are split
  // - local origin: for local events when external/local events are split and
  //   not used when external/local events are not split.
  SuccessRateMonitor external_origin_sr_monitor_;
  SuccessRateMonitor local_origin_sr_monitor_;

  void putResultNoLocalExternalSplit(Result result, absl::optional<uint64_t> code);
  void putResultWithLocalExternalSplit(Result result, absl::optional<uint64_t> code);
  std::function<void(DetectorHostMonitorImpl*, Result, absl::optional<uint64_t> code)>
      put_result_func_;
};

/**
 * All outlier detection stats. @see stats_macros.h
 */
#define ALL_OUTLIER_DETECTION_STATS(COUNTER, GAUGE)                                                \
  COUNTER(ejections_consecutive_5xx)                                                               \
  COUNTER(ejections_detected_consecutive_5xx)                                                      \
  COUNTER(ejections_detected_consecutive_gateway_failure)                                          \
  COUNTER(ejections_detected_success_rate)                                                         \
  COUNTER(ejections_detected_failure_percentage)                                                   \
  COUNTER(ejections_enforced_consecutive_5xx)                                                      \
  COUNTER(ejections_enforced_consecutive_gateway_failure)                                          \
  COUNTER(ejections_enforced_success_rate)                                                         \
  COUNTER(ejections_enforced_failure_percentage)                                                   \
  COUNTER(ejections_detected_consecutive_local_origin_failure)                                     \
  COUNTER(ejections_enforced_consecutive_local_origin_failure)                                     \
  COUNTER(ejections_detected_local_origin_success_rate)                                            \
  COUNTER(ejections_enforced_local_origin_success_rate)                                            \
  COUNTER(ejections_detected_local_origin_failure_percentage)                                      \
  COUNTER(ejections_enforced_local_origin_failure_percentage)                                      \
  COUNTER(ejections_enforced_total)                                                                \
  COUNTER(ejections_overflow)                                                                      \
  COUNTER(ejections_success_rate)                                                                  \
  COUNTER(ejections_total)                                                                         \
  GAUGE(ejections_active, Accumulate)

/**
 * Struct definition for all outlier detection stats. @see stats_macros.h
 */
struct DetectionStats {
  ALL_OUTLIER_DETECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the outlier detection.
 */
class DetectorConfig {
public:
  DetectorConfig(const envoy::config::cluster::v3::OutlierDetection& config);

  uint64_t intervalMs() const { return interval_ms_; }
  uint64_t baseEjectionTimeMs() const { return base_ejection_time_ms_; }
  uint64_t consecutive5xx() const { return consecutive_5xx_; }
  uint64_t consecutiveGatewayFailure() const { return consecutive_gateway_failure_; }
  uint64_t maxEjectionPercent() const { return max_ejection_percent_; }
  uint64_t successRateMinimumHosts() const { return success_rate_minimum_hosts_; }
  uint64_t successRateRequestVolume() const { return success_rate_request_volume_; }
  uint64_t successRateStdevFactor() const { return success_rate_stdev_factor_; }
  uint64_t failurePercentageThreshold() const { return failure_percentage_threshold_; }
  uint64_t failurePercentageMinimumHosts() const { return failure_percentage_minimum_hosts_; }
  uint64_t failurePercentageRequestVolume() const { return failure_percentage_request_volume_; }
  uint64_t enforcingConsecutive5xx() const { return enforcing_consecutive_5xx_; }
  uint64_t enforcingConsecutiveGatewayFailure() const {
    return enforcing_consecutive_gateway_failure_;
  }
  uint64_t enforcingSuccessRate() const { return enforcing_success_rate_; }
  uint64_t enforcingFailurePercentage() const { return enforcing_failure_percentage_; }
  uint64_t enforcingFailurePercentageLocalOrigin() const {
    return enforcing_failure_percentage_local_origin_;
  }
  bool splitExternalLocalOriginErrors() const { return split_external_local_origin_errors_; }
  uint64_t consecutiveLocalOriginFailure() const { return consecutive_local_origin_failure_; }
  uint64_t enforcingConsecutiveLocalOriginFailure() const {
    return enforcing_consecutive_local_origin_failure_;
  }
  uint64_t enforcingLocalOriginSuccessRate() const { return enforcing_local_origin_success_rate_; }

private:
  const uint64_t interval_ms_;
  const uint64_t base_ejection_time_ms_;
  const uint64_t consecutive_5xx_;
  const uint64_t consecutive_gateway_failure_;
  const uint64_t max_ejection_percent_;
  const uint64_t success_rate_minimum_hosts_;
  const uint64_t success_rate_request_volume_;
  const uint64_t success_rate_stdev_factor_;
  const uint64_t failure_percentage_threshold_;
  const uint64_t failure_percentage_minimum_hosts_;
  const uint64_t failure_percentage_request_volume_;
  const uint64_t enforcing_consecutive_5xx_;
  const uint64_t enforcing_consecutive_gateway_failure_;
  const uint64_t enforcing_success_rate_;
  const uint64_t enforcing_failure_percentage_;
  const uint64_t enforcing_failure_percentage_local_origin_;
  const bool split_external_local_origin_errors_;
  const uint64_t consecutive_local_origin_failure_;
  const uint64_t enforcing_consecutive_local_origin_failure_;
  const uint64_t enforcing_local_origin_success_rate_;

  static const uint64_t DEFAULT_INTERVAL_MS = 10000;
  static const uint64_t DEFAULT_BASE_EJECTION_TIME_MS = 30000;
  static const uint64_t DEFAULT_CONSECUTIVE_5XX = 5;
  static const uint64_t DEFAULT_CONSECUTIVE_GATEWAY_FAILURE = 5;
  static const uint64_t DEFAULT_MAX_EJECTION_PERCENT = 10;
  static const uint64_t DEFAULT_SUCCESS_RATE_MINIMUM_HOSTS = 5;
  static const uint64_t DEFAULT_SUCCESS_RATE_REQUEST_VOLUME = 100;
  static const uint64_t DEFAULT_SUCCESS_RATE_STDEV_FACTOR = 1900;
  static const uint64_t DEFAULT_FAILURE_PERCENTAGE_THRESHOLD = 85;
  static const uint64_t DEFAULT_FAILURE_PERCENTAGE_MINIMUM_HOSTS = 5;
  static const uint64_t DEFAULT_FAILURE_PERCENTAGE_REQUEST_VOLUME = 50;
  static const uint64_t DEFAULT_ENFORCING_CONSECUTIVE_5XX = 100;
  static const uint64_t DEFAULT_ENFORCING_CONSECUTIVE_GATEWAY_FAILURE = 0;
  static const uint64_t DEFAULT_ENFORCING_SUCCESS_RATE = 100;
  static const uint64_t DEFAULT_ENFORCING_FAILURE_PERCENTAGE = 0;
  static const uint64_t DEFAULT_ENFORCING_FAILURE_PERCENTAGE_LOCAL_ORIGIN = 0;
  static const uint64_t DEFAULT_CONSECUTIVE_LOCAL_ORIGIN_FAILURE = 5;
  static const uint64_t DEFAULT_ENFORCING_CONSECUTIVE_LOCAL_ORIGIN_FAILURE = 100;
  static const uint64_t DEFAULT_ENFORCING_LOCAL_ORIGIN_SUCCESS_RATE = 100;
};

/**
 * An implementation of an outlier detector. In the future we may support multiple outlier detection
 * implementations with different configuration. For now, as we iterate everything is contained
 * within this implementation.
 */
class DetectorImpl : public Detector, public std::enable_shared_from_this<DetectorImpl> {
public:
  static std::shared_ptr<DetectorImpl>
  create(const Cluster& cluster, const envoy::config::cluster::v3::OutlierDetection& config,
         Event::Dispatcher& dispatcher, Runtime::Loader& runtime, TimeSource& time_source,
         EventLoggerSharedPtr event_logger);
  ~DetectorImpl() override;

  void onConsecutive5xx(HostSharedPtr host);
  void onConsecutiveGatewayFailure(HostSharedPtr host);
  void onConsecutiveLocalOriginFailure(HostSharedPtr host);
  Runtime::Loader& runtime() { return runtime_; }
  DetectorConfig& config() { return config_; }

  // Upstream::Outlier::Detector
  void addChangedStateCb(ChangeStateCb cb) override { callbacks_.push_back(cb); }
  double
  successRateAverage(DetectorHostMonitor::SuccessRateMonitorType monitor_type) const override {
    return getSRNums(monitor_type).success_rate_average_;
  }
  double successRateEjectionThreshold(
      DetectorHostMonitor::SuccessRateMonitorType monitor_type) const override {
    return getSRNums(monitor_type).ejection_threshold_;
  }

  /**
   * This function returns pair of double values for success rate outlier detection. The pair
   * contains the average success rate of all valid hosts in the cluster and the ejection threshold.
   * If a host's success rate is under this threshold, the host is an outlier.
   * @param success_rate_sum is the sum of the data in the success_rate_data vector.
   * @param valid_success_rate_hosts is the vector containing the individual success rate data
   *        points.
   * @return EjectionPair
   */
  struct EjectionPair {
    double success_rate_average_; // average success rate of all valid hosts in the cluster
    double ejection_threshold_;   // ejection threshold for the cluster
  };
  static EjectionPair
  successRateEjectionThreshold(double success_rate_sum,
                               const std::vector<HostSuccessRatePair>& valid_success_rate_hosts,
                               double success_rate_stdev_factor);

private:
  DetectorImpl(const Cluster& cluster, const envoy::config::cluster::v3::OutlierDetection& config,
               Event::Dispatcher& dispatcher, Runtime::Loader& runtime, TimeSource& time_source,
               EventLoggerSharedPtr event_logger);

  void addHostMonitor(HostSharedPtr host);
  void armIntervalTimer();
  void checkHostForUneject(HostSharedPtr host, DetectorHostMonitorImpl* monitor, MonotonicTime now);
  void ejectHost(HostSharedPtr host, envoy::data::cluster::v2alpha::OutlierEjectionType type);
  static DetectionStats generateStats(Stats::Scope& scope);
  void initialize(const Cluster& cluster);
  void onConsecutiveErrorWorker(HostSharedPtr host,
                                envoy::data::cluster::v2alpha::OutlierEjectionType type);
  void notifyMainThreadConsecutiveError(HostSharedPtr host,
                                        envoy::data::cluster::v2alpha::OutlierEjectionType type);
  void onIntervalTimer();
  void runCallbacks(HostSharedPtr host);
  bool enforceEjection(envoy::data::cluster::v2alpha::OutlierEjectionType type);
  void updateEnforcedEjectionStats(envoy::data::cluster::v2alpha::OutlierEjectionType type);
  void updateDetectedEjectionStats(envoy::data::cluster::v2alpha::OutlierEjectionType type);
  void processSuccessRateEjections(DetectorHostMonitor::SuccessRateMonitorType monitor_type);

  DetectorConfig config_;
  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  DetectionStats stats_;
  Event::TimerPtr interval_timer_;
  std::list<ChangeStateCb> callbacks_;
  std::unordered_map<HostSharedPtr, DetectorHostMonitorImpl*> host_monitors_;
  EventLoggerSharedPtr event_logger_;

  // EjectionPair for external and local origin events.
  // When external/local origin events are not split, external_origin_sr_num_ are used for
  // both types of events: external and local. local_origin_sr_num_ is not used.
  // When external/local origin events are split, external_origin_sr_num_ are used only
  // for external events and local_origin_sr_num_ is used for local origin events.
  EjectionPair external_origin_sr_num_;
  EjectionPair local_origin_sr_num_;

  const EjectionPair& getSRNums(DetectorHostMonitor::SuccessRateMonitorType monitor_type) const {
    return (DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin == monitor_type)
               ? external_origin_sr_num_
               : local_origin_sr_num_;
  }
  EjectionPair& getSRNums(DetectorHostMonitor::SuccessRateMonitorType monitor_type) {
    return const_cast<EjectionPair&>(
        static_cast<const DetectorImpl&>(*this).getSRNums(monitor_type));
  }
};

class EventLoggerImpl : public EventLogger {
public:
  EventLoggerImpl(AccessLog::AccessLogManager& log_manager, const std::string& file_name,
                  TimeSource& time_source)
      : file_(log_manager.createAccessLog(file_name)), time_source_(time_source) {}

  // Upstream::Outlier::EventLogger
  void logEject(const HostDescriptionConstSharedPtr& host, Detector& detector,
                envoy::data::cluster::v2alpha::OutlierEjectionType type, bool enforced) override;

  void logUneject(const HostDescriptionConstSharedPtr& host) override;

private:
  void setCommonEventParams(envoy::data::cluster::v2alpha::OutlierDetectionEvent& event,
                            const HostDescriptionConstSharedPtr& host,
                            absl::optional<MonotonicTime> time);

  AccessLog::AccessLogFileSharedPtr file_;
  TimeSource& time_source_;
};

} // namespace Outlier
} // namespace Upstream
} // namespace Envoy
