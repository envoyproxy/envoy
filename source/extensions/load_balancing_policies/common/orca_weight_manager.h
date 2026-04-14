#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/logger.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace Common {

using OrcaLoadReportProto = xds::data::orca::v3::OrcaLoadReport;

/**
 * Configuration for OrcaWeightManager.
 */
struct OrcaWeightManagerConfig {
  std::vector<std::string> metric_names_for_computing_utilization;
  double error_utilization_penalty;
  std::chrono::milliseconds blackout_period;
  std::chrono::milliseconds weight_expiration_period;
  std::chrono::milliseconds weight_update_period;
};

struct OrcaHostLbPolicyData;

/**
 * Handles ORCA load reports and computes host weights.
 * Stores the config necessary to calculate host weight based on the report.
 */
class OrcaLoadReportHandler {
public:
  OrcaLoadReportHandler(const OrcaWeightManagerConfig& config, TimeSource& time_source);

  // Update client side data from `orca_load_report`. Invoked from `onOrcaLoadReport` callback of
  // OrcaHostLbPolicyData.
  absl::Status updateClientSideDataFromOrcaLoadReport(const OrcaLoadReportProto& orca_load_report,
                                                      OrcaHostLbPolicyData& client_side_data);

  // Get utilization from `orca_load_report` using named metrics specified in
  // `metric_names_for_computing_utilization`.
  static double getUtilizationFromOrcaReport(
      const OrcaLoadReportProto& orca_load_report,
      const std::vector<std::string>& metric_names_for_computing_utilization);

  // Calculate client side weight from `orca_load_report` using `getUtilizationFromOrcaReport()`,
  // QPS, EPS and `error_utilization_penalty`.
  static absl::StatusOr<uint32_t> calculateWeightFromOrcaReport(
      const OrcaLoadReportProto& orca_load_report,
      const std::vector<std::string>& metric_names_for_computing_utilization,
      double error_utilization_penalty);

private:
  const std::vector<std::string> metric_names_for_computing_utilization_;
  const double error_utilization_penalty_;
  TimeSource& time_source_;
};

using OrcaLoadReportHandlerSharedPtr = std::shared_ptr<OrcaLoadReportHandler>;

/**
 * Per-host LB policy data storing ORCA-derived weight and timestamps.
 * Hosts are not shared between different clusters, but are shared between load
 * balancer instances on different threads.
 */
struct OrcaHostLbPolicyData : public Envoy::Upstream::HostLbPolicyData {
  explicit OrcaHostLbPolicyData(OrcaLoadReportHandlerSharedPtr handler)
      : report_handler_(std::move(handler)) {}
  OrcaHostLbPolicyData(OrcaLoadReportHandlerSharedPtr handler, uint32_t weight,
                       MonotonicTime non_empty_since, MonotonicTime last_update_time)
      : report_handler_(std::move(handler)), weight_(weight), non_empty_since_(non_empty_since),
        last_update_time_(last_update_time) {}

  absl::Status onOrcaLoadReport(const Upstream::OrcaLoadReport& report,
                                const StreamInfo::StreamInfo& stream_info) override;

  // Update the weight and timestamps for first and last update time.
  void updateWeightNow(uint32_t weight, const MonotonicTime& now) {
    weight_.store(weight);
    last_update_time_.store(now);
    if (non_empty_since_.load() == kDefaultNonEmptySince) {
      non_empty_since_.store(now);
    }
  }

  // Get the weight if it was updated between max_non_empty_since and min_last_update_time,
  // otherwise return nullopt.
  absl::optional<uint32_t> getWeightIfValid(MonotonicTime max_non_empty_since,
                                            MonotonicTime min_last_update_time) {
    // If non_empty_since_ is too recent, we should use the default weight.
    if (max_non_empty_since < non_empty_since_.load()) {
      return absl::nullopt;
    }
    // If last update time is too old, we should use the default weight.
    if (last_update_time_.load() < min_last_update_time) {
      // Reset the non_empty_since_ time so the timer will start again.
      non_empty_since_.store(OrcaHostLbPolicyData::kDefaultNonEmptySince);
      return absl::nullopt;
    }
    return weight_;
  }

  OrcaLoadReportHandlerSharedPtr report_handler_;

  // Weight as calculated from the last load report.
  std::atomic<uint32_t> weight_ = 1;
  // Time when the weight is first updated. The weight is invalid if it is within of
  // `blackout_period_`.
  std::atomic<MonotonicTime> non_empty_since_ = kDefaultNonEmptySince;
  // Time when the weight is last updated. The weight is invalid if it is outside of
  // `expiration_period_`.
  std::atomic<MonotonicTime> last_update_time_ = kDefaultLastUpdateTime;

  static constexpr MonotonicTime kDefaultNonEmptySince = MonotonicTime::max();
  static constexpr MonotonicTime kDefaultLastUpdateTime = MonotonicTime::min();
};

/**
 * Manages ORCA-based weight computation for hosts in a priority set.
 * Extracted from ClientSideWeightedRoundRobinLoadBalancer to allow reuse.
 */
class OrcaWeightManager : protected Logger::Loggable<Logger::Id::upstream> {
public:
  using WeightsUpdatedCb = std::function<void()>;

  OrcaWeightManager(const OrcaWeightManagerConfig& config,
                    const Upstream::PrioritySet& priority_set, TimeSource& time_source,
                    Event::Dispatcher& dispatcher, WeightsUpdatedCb on_weights_updated);

  // Attach host data, register priority-update callback, start timer.
  absl::Status initialize();

  // Iterate priority sets, call on_weights_updated_ if changed.
  void updateWeightsOnMainThread();

  // Core weight update (blackout, expiration, median default). Returns true if any weight changed.
  bool updateWeightsOnHosts(const Upstream::HostVector& hosts);

  // Accessor for the report handler (used by tests and for creating host data).
  OrcaLoadReportHandlerSharedPtr reportHandler() { return report_handler_; }

  // Get weight based on host LB policy data if valid, otherwise return nullopt.
  static absl::optional<uint32_t> getWeightIfValidFromHost(const Upstream::Host& host,
                                                           MonotonicTime max_non_empty_since,
                                                           MonotonicTime min_last_update_time);

private:
  // Add LB policy data to all hosts that don't already have it.
  void addLbPolicyDataToHosts(const Upstream::HostVector& hosts);

  OrcaLoadReportHandlerSharedPtr report_handler_;
  const Upstream::PrioritySet& priority_set_;
  TimeSource& time_source_;

  // Timing parameters for the weight update.
  std::chrono::milliseconds blackout_period_;
  std::chrono::milliseconds weight_expiration_period_;
  std::chrono::milliseconds weight_update_period_;

  Event::TimerPtr weight_calculation_timer_;
  // Callback for priority_set_ updates.
  Envoy::Common::CallbackHandlePtr priority_update_cb_;
  // Callback invoked when weights are updated.
  WeightsUpdatedCb on_weights_updated_;
};

} // namespace Common
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
