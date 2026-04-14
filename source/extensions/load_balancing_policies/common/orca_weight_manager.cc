#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "envoy/upstream/upstream.h"

#include "source/common/orca/orca_load_metrics.h"

#include "absl/status/status.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace Common {

namespace {
std::string getHostAddress(const Upstream::Host* host) {
  if (host == nullptr || host->address() == nullptr) {
    return "unknown";
  }
  return host->address()->asString();
}
} // namespace

OrcaLoadReportHandler::OrcaLoadReportHandler(const OrcaWeightManagerConfig& config,
                                             TimeSource& time_source)
    : metric_names_for_computing_utilization_(config.metric_names_for_computing_utilization),
      error_utilization_penalty_(config.error_utilization_penalty), time_source_(time_source) {}

double OrcaLoadReportHandler::getUtilizationFromOrcaReport(
    const OrcaLoadReportProto& orca_load_report,
    const std::vector<std::string>& metric_names_for_computing_utilization) {
  // If application_utilization is valid, use it as the utilization metric.
  double utilization = orca_load_report.application_utilization();
  if (utilization > 0) {
    return utilization;
  }
  // Otherwise, find the most constrained utilization metric.
  utilization =
      Envoy::Orca::getMaxUtilization(metric_names_for_computing_utilization, orca_load_report);
  if (utilization > 0) {
    return utilization;
  }
  // If utilization is <= 0, use cpu_utilization.
  return orca_load_report.cpu_utilization();
}

absl::StatusOr<uint32_t> OrcaLoadReportHandler::calculateWeightFromOrcaReport(
    const OrcaLoadReportProto& orca_load_report,
    const std::vector<std::string>& metric_names_for_computing_utilization,
    double error_utilization_penalty) {
  double qps = orca_load_report.rps_fractional();
  if (qps <= 0) {
    return absl::InvalidArgumentError("QPS must be positive");
  }

  double utilization =
      getUtilizationFromOrcaReport(orca_load_report, metric_names_for_computing_utilization);
  // If there are errors, then increase utilization to lower the weight.
  utilization += error_utilization_penalty * orca_load_report.eps() / qps;

  if (utilization <= 0) {
    return absl::InvalidArgumentError("Utilization must be positive");
  }

  // Calculate the weight.
  double weight = qps / utilization;

  // Limit the weight to uint32_t max.
  if (weight > std::numeric_limits<uint32_t>::max()) {
    weight = std::numeric_limits<uint32_t>::max();
  }
  return weight;
}

absl::Status OrcaLoadReportHandler::updateClientSideDataFromOrcaLoadReport(
    const OrcaLoadReportProto& orca_load_report, OrcaHostLbPolicyData& client_side_data) {
  const absl::StatusOr<uint32_t> weight = calculateWeightFromOrcaReport(
      orca_load_report, metric_names_for_computing_utilization_, error_utilization_penalty_);
  if (!weight.ok()) {
    return weight.status();
  }

  // Update client side data attached to the host.
  client_side_data.updateWeightNow(weight.value(), time_source_.monotonicTime());
  return absl::OkStatus();
}

absl::Status OrcaHostLbPolicyData::onOrcaLoadReport(const Upstream::OrcaLoadReport& report,
                                                    const StreamInfo::StreamInfo&) {
  ASSERT(report_handler_ != nullptr);
  return report_handler_->updateClientSideDataFromOrcaLoadReport(report, *this);
}

OrcaWeightManager::OrcaWeightManager(const OrcaWeightManagerConfig& config,
                                     const Upstream::PrioritySet& priority_set,
                                     TimeSource& time_source, Event::Dispatcher& dispatcher,
                                     WeightsUpdatedCb on_weights_updated)
    : report_handler_(std::make_shared<OrcaLoadReportHandler>(config, time_source)),
      priority_set_(priority_set), time_source_(time_source),
      blackout_period_(config.blackout_period),
      weight_expiration_period_(config.weight_expiration_period),
      weight_update_period_(config.weight_update_period),
      on_weights_updated_(std::move(on_weights_updated)) {
  weight_calculation_timer_ = dispatcher.createTimer([this]() -> void {
    updateWeightsOnMainThread();
    weight_calculation_timer_->enableTimer(weight_update_period_);
  });
}

absl::Status OrcaWeightManager::initialize() {
  // Ensure that all hosts have LB policy data.
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    addLbPolicyDataToHosts(host_set->hosts());
  }

  // Setup a callback to receive priority set updates.
  priority_update_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t, const Upstream::HostVector& hosts_added, const Upstream::HostVector&) {
        addLbPolicyDataToHosts(hosts_added);
        updateWeightsOnMainThread();
      });

  weight_calculation_timer_->enableTimer(weight_update_period_);

  return absl::OkStatus();
}

void OrcaWeightManager::updateWeightsOnMainThread() {
  ENVOY_LOG(trace, "updateWeightsOnMainThread");
  bool updated = false;
  // Update weights on hosts in priority set of the thread aware load balancer
  // on the main thread.
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    updated = updateWeightsOnHosts(host_set->hosts()) || updated;
  }
  if (updated) {
    on_weights_updated_();
  }
}

bool OrcaWeightManager::updateWeightsOnHosts(const Upstream::HostVector& hosts) {
  std::vector<uint32_t> weights;
  Upstream::HostVector hosts_with_default_weight;
  bool weights_updated = false;
  const MonotonicTime now = time_source_.monotonicTime();
  // Weight is considered invalid (too recent) if it was first updated within `blackout_period_`.
  const MonotonicTime max_non_empty_since = now - blackout_period_;
  // Weight is considered invalid (too old) if it was last updated before
  // `weight_expiration_period_`.
  const MonotonicTime min_last_update_time = now - weight_expiration_period_;
  weights.reserve(hosts.size());
  hosts_with_default_weight.reserve(hosts.size());
  ENVOY_LOG(trace, "updateWeights hosts.size() = {}, time since epoch = {}", hosts.size(),
            now.time_since_epoch().count());
  // Scan through all hosts and update their weights if they are valid.
  for (const auto& host_ptr : hosts) {
    // Get client side weight or `nullopt` if it is invalid (see above).
    absl::optional<uint32_t> client_side_weight =
        getWeightIfValidFromHost(*host_ptr, max_non_empty_since, min_last_update_time);
    // If `client_side_weight` is valid, then set it as the host weight and store it in
    // `weights` to calculate median valid weight across all hosts.
    if (client_side_weight.has_value()) {
      const uint32_t new_weight = client_side_weight.value();
      weights.push_back(new_weight);
      if (new_weight != host_ptr->weight()) {
        host_ptr->weight(new_weight);
        ENVOY_LOG(trace, "updateWeights hostWeight {} = {}", getHostAddress(host_ptr.get()),
                  host_ptr->weight());
        weights_updated = true;
      }
    } else {
      // If `client_side_weight` is invalid, then set host to default (median) weight.
      hosts_with_default_weight.push_back(host_ptr);
    }
  }
  // If some hosts don't have valid weight, then update them with default weight.
  if (!hosts_with_default_weight.empty()) {
    // Calculate the default weight as median of all valid weights.
    uint32_t default_weight = 1;
    if (!weights.empty()) {
      const auto median_it = weights.begin() + weights.size() / 2;
      std::nth_element(weights.begin(), median_it, weights.end());
      if (weights.size() % 2 == 1) {
        default_weight = *median_it;
      } else {
        // If the number of weights is even, then the median is the average of the two middle
        // elements.
        const auto lower_median_it = std::max_element(weights.begin(), median_it);
        // Use uint64_t to avoid potential overflow of the weights sum.
        default_weight = static_cast<uint32_t>(
            (static_cast<uint64_t>(*lower_median_it) + static_cast<uint64_t>(*median_it)) / 2);
      }
    }
    // Update the hosts with default weight.
    for (const auto& host_ptr : hosts_with_default_weight) {
      if (default_weight != host_ptr->weight()) {
        host_ptr->weight(default_weight);
        ENVOY_LOG(trace, "updateWeights default hostWeight {} = {}", getHostAddress(host_ptr.get()),
                  host_ptr->weight());
        weights_updated = true;
      }
    }
  }
  return weights_updated;
}

void OrcaWeightManager::addLbPolicyDataToHosts(const Upstream::HostVector& hosts) {
  for (const auto& host_ptr : hosts) {
    if (!host_ptr->lbPolicyData().has_value()) {
      ENVOY_LOG(trace, "Adding LB policy data to Host {}", getHostAddress(host_ptr.get()));
      host_ptr->setLbPolicyData(std::make_unique<OrcaHostLbPolicyData>(report_handler_));
    }
  }
}

absl::optional<uint32_t>
OrcaWeightManager::getWeightIfValidFromHost(const Upstream::Host& host,
                                            MonotonicTime max_non_empty_since,
                                            MonotonicTime min_last_update_time) {
  auto client_side_data = host.typedLbPolicyData<OrcaHostLbPolicyData>();
  if (!client_side_data.has_value()) {
    ENVOY_LOG_MISC(trace, "Host does not have OrcaHostLbPolicyData {}", getHostAddress(&host));
    return absl::nullopt;
  }
  return client_side_data->getWeightIfValid(max_non_empty_since, min_last_update_time);
}

} // namespace Common
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
