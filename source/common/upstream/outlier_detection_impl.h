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
#include "envoy/api/v2/cluster/outlier_detection.pb.h"
#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/runtime/runtime.h"
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
  void putResult(Result) override {}
  void putResponseTime(std::chrono::milliseconds) override {}
  const Optional<MonotonicTime>& lastEjectionTime() override { return time_; }
  const Optional<MonotonicTime>& lastUnejectionTime() override { return time_; }
  double successRate() const override { return -1; }

private:
  const Optional<MonotonicTime> time_;
};

/**
 * Factory for creating a detector from a proto configuration.
 */
class DetectorImplFactory {
public:
  static DetectorSharedPtr createForCluster(Cluster& cluster,
                                            const envoy::api::v2::Cluster& cluster_config,
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
   * @return a valid Optional<double> with the success rate. If there were not enough requests, an
   *         invalid Optional<double> is returned.
   */
  Optional<double> getSuccessRate(uint64_t success_rate_request_volume);

private:
  std::unique_ptr<SuccessRateAccumulatorBucket> current_success_rate_bucket_;
  std::unique_ptr<SuccessRateAccumulatorBucket> backup_success_rate_bucket_;
};

class DetectorImpl;

/**
 * Implementation of DetectorHostMonitor for the generic detector.
 */
class DetectorHostMonitorImpl : public DetectorHostMonitor {
public:
  DetectorHostMonitorImpl(std::shared_ptr<DetectorImpl> detector, HostSharedPtr host)
      : detector_(detector), host_(host), success_rate_(-1) {
    // Point the success_rate_accumulator_bucket_ pointer to a bucket.
    updateCurrentSuccessRateBucket();
  }

  void eject(MonotonicTime ejection_time);
  void uneject(MonotonicTime ejection_time);
  void updateCurrentSuccessRateBucket();
  SuccessRateAccumulator& successRateAccumulator() { return success_rate_accumulator_; }
  void successRate(double new_success_rate) { success_rate_ = new_success_rate; }
  void resetConsecutive5xx() { consecutive_5xx_ = 0; }
  void resetConsecutiveGatewayFailure() { consecutive_gateway_failure_ = 0; }
  static Http::Code resultToHttpCode(Result result);

  // Upstream::Outlier::DetectorHostMonitor
  uint32_t numEjections() override { return num_ejections_; }
  void putHttpResponseCode(uint64_t response_code) override;
  void putResult(Result result) override;
  void putResponseTime(std::chrono::milliseconds) override {}
  const Optional<MonotonicTime>& lastEjectionTime() override { return last_ejection_time_; }
  const Optional<MonotonicTime>& lastUnejectionTime() override { return last_unejection_time_; }
  double successRate() const override { return success_rate_; }

private:
  std::weak_ptr<DetectorImpl> detector_;
  std::weak_ptr<Host> host_;
  std::atomic<uint32_t> consecutive_5xx_{0};
  std::atomic<uint32_t> consecutive_gateway_failure_{0};
  Optional<MonotonicTime> last_ejection_time_;
  Optional<MonotonicTime> last_unejection_time_;
  uint32_t num_ejections_{};
  SuccessRateAccumulator success_rate_accumulator_;
  std::atomic<SuccessRateAccumulatorBucket*> success_rate_accumulator_bucket_;
  double success_rate_;
};

/**
 * All outlier detection stats. @see stats_macros.h
 */
// clang-format off
#define ALL_OUTLIER_DETECTION_STATS(COUNTER, GAUGE)                                                \
  COUNTER(ejections_total)                                                                         \
  GAUGE  (ejections_active)                                                                        \
  COUNTER(ejections_overflow)                                                                      \
  COUNTER(ejections_consecutive_5xx)                                                               \
  COUNTER(ejections_success_rate)                                                                  \
  COUNTER(ejections_enforced_total)                                                                \
  COUNTER(ejections_detected_consecutive_5xx)                                                      \
  COUNTER(ejections_enforced_consecutive_5xx)                                                      \
  COUNTER(ejections_detected_success_rate)                                                         \
  COUNTER(ejections_enforced_success_rate)                                                         \
  COUNTER(ejections_detected_consecutive_gateway_failure)                                          \
  COUNTER(ejections_enforced_consecutive_gateway_failure)
// clang-format on

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
  DetectorConfig(const envoy::api::v2::cluster::OutlierDetection& config);

  uint64_t intervalMs() { return interval_ms_; }
  uint64_t baseEjectionTimeMs() { return base_ejection_time_ms_; }
  uint64_t consecutive5xx() { return consecutive_5xx_; }
  uint64_t consecutiveGatewayFailure() { return consecutive_gateway_failure_; }
  uint64_t maxEjectionPercent() { return max_ejection_percent_; }
  uint64_t successRateMinimumHosts() { return success_rate_minimum_hosts_; }
  uint64_t successRateRequestVolume() { return success_rate_request_volume_; }
  uint64_t successRateStdevFactor() { return success_rate_stdev_factor_; }
  uint64_t enforcingConsecutive5xx() { return enforcing_consecutive_5xx_; }
  uint64_t enforcingConsecutiveGatewayFailure() { return enforcing_consecutive_gateway_failure_; }
  uint64_t enforcingSuccessRate() { return enforcing_success_rate_; }

private:
  const uint64_t interval_ms_;
  const uint64_t base_ejection_time_ms_;
  const uint64_t consecutive_5xx_;
  const uint64_t consecutive_gateway_failure_;
  const uint64_t max_ejection_percent_;
  const uint64_t success_rate_minimum_hosts_;
  const uint64_t success_rate_request_volume_;
  const uint64_t success_rate_stdev_factor_;
  const uint64_t enforcing_consecutive_5xx_;
  const uint64_t enforcing_consecutive_gateway_failure_;
  const uint64_t enforcing_success_rate_;
};

/**
 * An implementation of an outlier detector. In the future we may support multiple outlier detection
 * implementations with different configuration. For now, as we iterate everything is contained
 * within this implementation.
 */
class DetectorImpl : public Detector, public std::enable_shared_from_this<DetectorImpl> {
public:
  static std::shared_ptr<DetectorImpl>
  create(const Cluster& cluster, const envoy::api::v2::cluster::OutlierDetection& config,
         Event::Dispatcher& dispatcher, Runtime::Loader& runtime, MonotonicTimeSource& time_source,
         EventLoggerSharedPtr event_logger);
  ~DetectorImpl();

  void onConsecutive5xx(HostSharedPtr host);
  void onConsecutiveGatewayFailure(HostSharedPtr host);
  Runtime::Loader& runtime() { return runtime_; }
  DetectorConfig& config() { return config_; }

  // Upstream::Outlier::Detector
  void addChangedStateCb(ChangeStateCb cb) override { callbacks_.push_back(cb); }
  double successRateAverage() const override { return success_rate_average_; }
  double successRateEjectionThreshold() const override { return success_rate_ejection_threshold_; }

private:
  DetectorImpl(const Cluster& cluster, const envoy::api::v2::cluster::OutlierDetection& config,
               Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
               MonotonicTimeSource& time_source, EventLoggerSharedPtr event_logger);

  void addHostMonitor(HostSharedPtr host);
  void armIntervalTimer();
  void checkHostForUneject(HostSharedPtr host, DetectorHostMonitorImpl* monitor, MonotonicTime now);
  void ejectHost(HostSharedPtr host, EjectionType type);
  static DetectionStats generateStats(Stats::Scope& scope);
  void initialize(const Cluster& cluster);
  void onConsecutiveErrorWorker(HostSharedPtr host, EjectionType type);
  void notifyMainThreadConsecutiveError(HostSharedPtr host, EjectionType type);
  void onIntervalTimer();
  void runCallbacks(HostSharedPtr host);
  bool enforceEjection(EjectionType type);
  void updateEnforcedEjectionStats(EjectionType type);
  void processSuccessRateEjections();

  DetectorConfig config_;
  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  MonotonicTimeSource& time_source_;
  DetectionStats stats_;
  Event::TimerPtr interval_timer_;
  std::list<ChangeStateCb> callbacks_;
  std::unordered_map<HostSharedPtr, DetectorHostMonitorImpl*> host_monitors_;
  EventLoggerSharedPtr event_logger_;
  double success_rate_average_;
  double success_rate_ejection_threshold_;
};

class EventLoggerImpl : public EventLogger {
public:
  EventLoggerImpl(AccessLog::AccessLogManager& log_manager, const std::string& file_name,
                  SystemTimeSource& time_source, MonotonicTimeSource& monotonic_time_source)
      : file_(log_manager.createAccessLog(file_name)), time_source_(time_source),
        monotonic_time_source_(monotonic_time_source) {}

  // Upstream::Outlier::EventLogger
  void logEject(HostDescriptionConstSharedPtr host, Detector& detector, EjectionType type,
                bool enforced) override;
  void logUneject(HostDescriptionConstSharedPtr host) override;

private:
  std::string typeToString(EjectionType type);
  int secsSinceLastAction(const Optional<MonotonicTime>& lastActionTime, MonotonicTime now);

  Filesystem::FileSharedPtr file_;
  SystemTimeSource& time_source_;
  MonotonicTimeSource& monotonic_time_source_;
};

/**
 * Utilities for Outlier Detection.
 */
class Utility {
public:
  struct EjectionPair {
    double success_rate_average_;
    double ejection_threshold_;
  };

  /**
   * This function returns an EjectionPair for success rate outlier detection. The pair contains
   * the average success rate of all valid hosts in the cluster and the ejection threshold.
   * If a host's success rate is under this threshold, the host is an outlier.
   * @param success_rate_sum is the sum of the data in the success_rate_data vector.
   * @param valid_success_rate_hosts is the vector containing the individual success rate data
   *        points.
   * @return EjectionPair.
   */
  static EjectionPair
  successRateEjectionThreshold(double success_rate_sum,
                               const std::vector<HostSuccessRatePair>& valid_success_rate_hosts,
                               double success_rate_stdev_factor);
};

} // namespace Outlier
} // namespace Upstream
} // namespace Envoy
