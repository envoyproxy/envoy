#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "common/json/json_loader.h"

namespace Upstream {
namespace Outlier {

/**
 * Null host sink implementation.
 */
class DetectorHostSinkNullImpl : public DetectorHostSink {
public:
  // Upstream::Outlier::DetectorHostSink
  uint32_t numEjections() override { return 0; }
  void putHttpResponseCode(uint64_t) override {}
  void putResponseTime(std::chrono::milliseconds) override {}
  const Optional<SystemTime>& lastEjectionTime() override { return time_; }
  const Optional<SystemTime>& lastUnejectionTime() override { return time_; }

private:
  const Optional<SystemTime> time_;
};

/**
 * Factory for creating a detector from a JSON configuration.
 */
class DetectorImplFactory {
public:
  static DetectorPtr createForCluster(Cluster& cluster, const Json::Object& cluster_config,
                                      Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                                      EventLoggerPtr event_logger);
};

struct SRAccumulatorBucket {
  std::atomic<uint64_t> success_rq_counter_;
  std::atomic<uint64_t> total_rq_counter_;
};

/**
 * The S(uccess)R(ate)AccumulatorImpl uses the SRAccumulatorBucket to get per host Success Rate
 * stats. This implementation has a fixed window size of time, and thus only needs a
 * bucket to write to, and a bucket to accumulate/run stats over.
 */
class SRAccumulatorImpl {
public:
  SRAccumulatorImpl()
      : current_sr_bucket_(new SRAccumulatorBucket()),
        backup_sr_bucket_(new SRAccumulatorBucket()) {}

  SRAccumulatorBucket* getCurrentWriter();
  /**
   * This function returns the SR of a host over a window of time if the request volume is high
   * enough. The underlying window of time could be dynamically adjusted. In the current
   * implementation it is a fixed time window.
   * @param rq_volume_threshold the threshold of requests an accumulator has to have in order to be
   *                            able to return a significant SR value.
   * @return a valid Optional<double> with the success rate. If there were not enough requests, an
   *         invalid Optional<double> is returned.
   */
  Optional<double> getSR(uint64_t rq_volume_threshold);

private:
  std::unique_ptr<SRAccumulatorBucket> current_sr_bucket_;
  std::unique_ptr<SRAccumulatorBucket> backup_sr_bucket_;
};

class DetectorImpl;

/**
 * Implementation of DetectorHostSink for the generic detector.
 */
class DetectorHostSinkImpl : public DetectorHostSink {
public:
  DetectorHostSinkImpl(std::shared_ptr<DetectorImpl> detector, HostPtr host)
      : detector_(detector), host_(host) {
    // Point the sr_accumulator_bucket_ pointer to a bucket.
    updateCurrentSRBucket();
  }

  void eject(SystemTime ejection_time);
  void uneject(SystemTime ejection_time);
  void updateCurrentSRBucket();
  SRAccumulatorImpl& srAccumulator() { return sr_accumulator_; };

  // Upstream::Outlier::DetectorHostSink
  uint32_t numEjections() override { return num_ejections_; }
  void putHttpResponseCode(uint64_t response_code) override;
  void putResponseTime(std::chrono::milliseconds) override {}
  const Optional<SystemTime>& lastEjectionTime() { return last_ejection_time_; }
  const Optional<SystemTime>& lastUnejectionTime() { return last_unejection_time_; }

private:
  std::weak_ptr<DetectorImpl> detector_;
  std::weak_ptr<Host> host_;
  std::atomic<uint32_t> consecutive_5xx_{0};
  Optional<SystemTime> last_ejection_time_;
  Optional<SystemTime> last_unejection_time_;
  uint32_t num_ejections_{};
  SRAccumulatorImpl sr_accumulator_;
  std::atomic<SRAccumulatorBucket*> sr_accumulator_bucket_;
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
  COUNTER(ejections_sr)
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
  DetectorConfig(const Json::Object& json_config);

  uint64_t intervalMs() { return interval_ms_; }
  uint64_t baseEjectionTimeMs() { return base_ejection_time_ms_; }
  uint64_t consecutive5xx() { return consecutive_5xx_; }
  uint64_t maxEjectionPercent() { return max_ejection_percent_; }
  uint64_t significantHostThreshold() { return significant_host_threshold_; }
  uint64_t rqVolumeThreshold() { return rq_volume_threshold_; }
  uint64_t enforcingConsecutive5xx() { return enforcing_consecutive_5xx_; }
  uint64_t enforcingSR() { return enforcing_sr_; }

private:
  const uint64_t interval_ms_;
  const uint64_t base_ejection_time_ms_;
  const uint64_t consecutive_5xx_;
  const uint64_t max_ejection_percent_;
  const uint64_t significant_host_threshold_;
  const uint64_t rq_volume_threshold_;
  const uint64_t enforcing_consecutive_5xx_;
  const uint64_t enforcing_sr_;
};

/**
 * An implementation of an outlier detector. In the future we may support multiple outlier detection
 * implementations with different configuration. For now, as we iterate everything is contained
 * within this implementation.
 */
class DetectorImpl : public Detector, public std::enable_shared_from_this<DetectorImpl> {
public:
  static std::shared_ptr<DetectorImpl>
  create(const Cluster& cluster, const Json::Object& json_config, Event::Dispatcher& dispatcher,
         Runtime::Loader& runtime, SystemTimeSource& time_source, EventLoggerPtr event_logger);
  ~DetectorImpl();

  void onConsecutive5xx(HostPtr host);
  Runtime::Loader& runtime() { return runtime_; }
  DetectorConfig& config() { return config_; }

  // Upstream::Outlier::Detector
  void addChangedStateCb(ChangeStateCb cb) override { callbacks_.push_back(cb); }

private:
  DetectorImpl(const Cluster& cluster, const Json::Object& json_config,
               Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
               SystemTimeSource& time_source, EventLoggerPtr event_logger);

  void addHostSink(HostPtr host);
  void armIntervalTimer();
  void checkHostForUneject(HostPtr host, DetectorHostSinkImpl* sink, SystemTime now);
  void ejectHost(HostPtr host, EjectionType type);
  static DetectionStats generateStats(Stats::Scope& scope);
  void initialize(const Cluster& cluster);
  void onConsecutive5xxWorker(HostPtr host);
  void onIntervalTimer();
  void runCallbacks(HostPtr host);
  bool enforceEjection(EjectionType type);

  DetectorConfig config_;
  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  SystemTimeSource& time_source_;
  DetectionStats stats_;
  Event::TimerPtr interval_timer_;
  std::list<ChangeStateCb> callbacks_;
  std::unordered_map<HostPtr, DetectorHostSinkImpl*> host_sinks_;
  EventLoggerPtr event_logger_;
};

class EventLoggerImpl : public EventLogger {
public:
  EventLoggerImpl(AccessLog::AccessLogManager& log_manager, const std::string& file_name,
                  SystemTimeSource& time_source)
      : file_(log_manager.createAccessLog(file_name)), time_source_(time_source) {}

  // Upstream::Outlier::EventLogger
  void logEject(HostDescriptionPtr host, EjectionType type) override;
  void logUneject(HostDescriptionPtr host) override;

private:
  std::string typeToString(EjectionType type);
  int secsSinceLastAction(const Optional<SystemTime>& lastActionTime, SystemTime now);

  Filesystem::FilePtr file_;
  SystemTimeSource& time_source_;
};

/**
 * Utilities for Outlier Detection
 */
class Utility {
public:
  /**
   * This function returns the Success Rate trheshold for Success Rate outlier detection. If a
   * host's Success Rate is under this threshold the host is an outlier.
   * @param sr_sum is the sum of the data in the sr_data vector.
   * @param sr_data is the vector containing the individual success rate data points.
   * @return the Success Rate threshold.
   */
  static double srEjectionThreshold(double sr_sum, std::vector<double>& sr_data);

private:
  // Factor to multiply the stdev of a cluster's Success Rate for success rate outlier ejection.
  static const double SR_STDEV_FACTOR;
};

} // Outlier
} // Upstream
