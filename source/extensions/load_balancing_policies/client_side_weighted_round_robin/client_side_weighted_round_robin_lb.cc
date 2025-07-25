#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/common/time.h"
#include "envoy/upstream/upstream.h"

#include "source/common/orca/orca_load_metrics.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/status/status.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Upstream {

namespace {
std::string getHostAddress(const Host* host) {
  if (host == nullptr || host->address() == nullptr) {
    return "unknown";
  }
  return host->address()->asString();
}

envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin
getRoundRobinConfig(const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config) {
  TypedRoundRobinLbConfig round_robin_config(common_config, Upstream::LegacyRoundRobinLbProto());
  return round_robin_config.lb_config_;
}

} // namespace

ClientSideWeightedRoundRobinLbConfig::ClientSideWeightedRoundRobinLbConfig(
    const ClientSideWeightedRoundRobinLbProto& lb_proto, Event::Dispatcher& main_thread_dispatcher,
    ThreadLocal::SlotAllocator& tls_slot_allocator)
    : main_thread_dispatcher_(main_thread_dispatcher), tls_slot_allocator_(tls_slot_allocator) {
  ENVOY_LOG_MISC(trace, "ClientSideWeightedRoundRobinLbConfig config {}", lb_proto.DebugString());
  metric_names_for_computing_utilization =
      std::vector<std::string>(lb_proto.metric_names_for_computing_utilization().begin(),
                               lb_proto.metric_names_for_computing_utilization().end());
  error_utilization_penalty = lb_proto.error_utilization_penalty().value();
  blackout_period =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(lb_proto, blackout_period, 10000));
  weight_expiration_period = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(lb_proto, weight_expiration_period, 180000));
  weight_update_period =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(lb_proto, weight_update_period, 1000));
}

ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb::WorkerLocalLb(
    const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
    TimeSource& time_source, OptRef<ThreadLocalShim> tls_shim)
    : RoundRobinLoadBalancer(priority_set, local_priority_set, stats, runtime, random,
                             PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
                                 common_config, healthy_panic_threshold, 100, 50),
                             getRoundRobinConfig(common_config), time_source) {
  if (tls_shim.has_value()) {
    apply_weights_cb_handle_ = tls_shim->apply_weights_cb_helper_.add([this](uint32_t priority) {
      refresh(priority);
      return absl::OkStatus();
    });
  }
}

ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::OrcaLoadReportHandler(
    const ClientSideWeightedRoundRobinLbConfig& lb_config, TimeSource& time_source)
    : metric_names_for_computing_utilization_(lb_config.metric_names_for_computing_utilization),
      error_utilization_penalty_(lb_config.error_utilization_penalty), time_source_(time_source) {}

void ClientSideWeightedRoundRobinLoadBalancer::initFromConfig(
    const ClientSideWeightedRoundRobinLbConfig& lb_config) {
  blackout_period_ = lb_config.blackout_period;
  weight_expiration_period_ = lb_config.weight_expiration_period;
  weight_update_period_ = lb_config.weight_update_period;
}

void ClientSideWeightedRoundRobinLoadBalancer::updateWeightsOnMainThread() {
  ENVOY_LOG(trace, "updateWeightsOnMainThread");
  for (const HostSetPtr& host_set : priority_set_.hostSetsPerPriority()) {
    if (updateWeightsOnHosts(host_set->hosts())) {
      // If weights have changed, then apply them to all workers.
      factory_->applyWeightsToAllWorkers(host_set->priority());
    }
  }
}

bool ClientSideWeightedRoundRobinLoadBalancer::updateWeightsOnHosts(const HostVector& hosts) {
  std::vector<uint32_t> weights;
  HostVector hosts_with_default_weight;
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
        getClientSideWeightIfValidFromHost(*host_ptr, max_non_empty_since, min_last_update_time);
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

void ClientSideWeightedRoundRobinLoadBalancer::addClientSideLbPolicyDataToHosts(
    const HostVector& hosts) {
  for (const auto& host_ptr : hosts) {
    if (!host_ptr->lbPolicyData().has_value()) {
      ENVOY_LOG(trace, "Adding LB policy data to Host {}", getHostAddress(host_ptr.get()));
      host_ptr->setLbPolicyData(std::make_unique<ClientSideHostLbPolicyData>(report_handler_));
    }
  }
}

absl::Status ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData::onOrcaLoadReport(
    const Upstream::OrcaLoadReport& report) {
  ASSERT(report_handler_ != nullptr);
  return report_handler_->updateClientSideDataFromOrcaLoadReport(report, *this);
}

absl::optional<uint32_t>
ClientSideWeightedRoundRobinLoadBalancer::getClientSideWeightIfValidFromHost(
    const Host& host, MonotonicTime max_non_empty_since, MonotonicTime min_last_update_time) {
  auto client_side_data = host.typedLbPolicyData<ClientSideHostLbPolicyData>();
  if (!client_side_data.has_value()) {
    ENVOY_LOG(trace, "Host does not have ClientSideHostLbPolicyData {}", getHostAddress(&host));
    return std::nullopt;
  }
  return client_side_data->getWeightIfValid(max_non_empty_since, min_last_update_time);
}

double
ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::getUtilizationFromOrcaReport(
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

absl::StatusOr<uint32_t>
ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::calculateWeightFromOrcaReport(
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

absl::Status ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::
    updateClientSideDataFromOrcaLoadReport(const OrcaLoadReportProto& orca_load_report,
                                           ClientSideHostLbPolicyData& client_side_data) {
  const absl::StatusOr<uint32_t> weight = calculateWeightFromOrcaReport(
      orca_load_report, metric_names_for_computing_utilization_, error_utilization_penalty_);
  if (!weight.ok()) {
    return weight.status();
  }

  // Update client side data attached to the host.
  client_side_data.updateWeightNow(weight.value(), time_source_.monotonicTime());
  return absl::OkStatus();
}

Upstream::LoadBalancerPtr ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLbFactory::create(
    Upstream::LoadBalancerParams params) {
  return std::make_unique<Upstream::ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb>(
      params.priority_set, params.local_priority_set, cluster_info_.lbStats(), runtime_, random_,
      cluster_info_.lbConfig(), time_source_, tls_->get());
}

void ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLbFactory::applyWeightsToAllWorkers(
    uint32_t priority) {
  tls_->runOnAllThreads([priority](OptRef<ThreadLocalShim> tls_shim) -> void {
    if (tls_shim.has_value()) {
      auto status = tls_shim->apply_weights_cb_helper_.runCallbacks(priority);
    }
  });
}

ClientSideWeightedRoundRobinLoadBalancer::ClientSideWeightedRoundRobinLoadBalancer(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source)
    : cluster_info_(cluster_info), priority_set_(priority_set), runtime_(runtime), random_(random),
      time_source_(time_source) {

  const auto* typed_lb_config =
      dynamic_cast<const ClientSideWeightedRoundRobinLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr);
  report_handler_ = std::make_shared<OrcaLoadReportHandler>(*typed_lb_config, time_source_);
  factory_ =
      std::make_shared<WorkerLocalLbFactory>(cluster_info, priority_set, runtime, random,
                                             time_source, typed_lb_config->tls_slot_allocator_);

  initFromConfig(*typed_lb_config);

  weight_calculation_timer_ =
      typed_lb_config->main_thread_dispatcher_.createTimer([this]() -> void {
        updateWeightsOnMainThread();
        weight_calculation_timer_->enableTimer(weight_update_period_);
      });
}

absl::Status ClientSideWeightedRoundRobinLoadBalancer::initialize() {
  // Ensure that all hosts have client side lb policy data.
  for (const HostSetPtr& host_set : priority_set_.hostSetsPerPriority()) {
    addClientSideLbPolicyDataToHosts(host_set->hosts());
  }

  // Setup a callback to receive priority set updates.
  priority_update_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector&) -> absl::Status {
        addClientSideLbPolicyDataToHosts(hosts_added);
        updateWeightsOnMainThread();
        return absl::OkStatus();
      });

  weight_calculation_timer_->enableTimer(weight_update_period_);

  return absl::OkStatus();
}

} // namespace Upstream
} // namespace Envoy
