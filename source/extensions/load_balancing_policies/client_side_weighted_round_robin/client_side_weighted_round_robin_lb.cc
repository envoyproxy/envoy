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
} // namespace

ClientSideWeightedRoundRobinLbConfig::ClientSideWeightedRoundRobinLbConfig(
    const ClientSideWeightedRoundRobinLbProto& lb_proto, Event::Dispatcher& main_thread_dispatcher)
    : main_thread_dispatcher_(main_thread_dispatcher) {
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
    const ClientSideWeightedRoundRobinLbConfig& client_side_weighted_round_robin_config,
    TimeSource& time_source)
    : RoundRobinLoadBalancer(priority_set, local_priority_set, stats, runtime, random,
                             common_config,
                             /*round_robin_config=*/std::nullopt, time_source) {
  orca_load_report_handler_ =
      std::make_shared<OrcaLoadReportHandler>(client_side_weighted_round_robin_config, time_source);
}

HostConstSharedPtr
ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb::chooseHost(LoadBalancerContext* context) {
  HostConstSharedPtr host = RoundRobinLoadBalancer::chooseHost(context);
  if (context != nullptr) {
    // Configure callbacks to receive ORCA load report.
    context->setOrcaLoadReportCallbacks(orca_load_report_handler_);
  }
  return host;
}

ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::OrcaLoadReportHandler(
    const ClientSideWeightedRoundRobinLbConfig& lb_config, TimeSource& time_source)
    : metric_names_for_computing_utilization_(lb_config.metric_names_for_computing_utilization),
      error_utilization_penalty_(lb_config.error_utilization_penalty), time_source_(time_source) {}

absl::Status ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::onOrcaLoadReport(
    const OrcaLoadReportProto& orca_load_report, const HostDescription& host_description) {
  const Host* host = dynamic_cast<const Host*>(&host_description);
  ENVOY_BUG(host != nullptr, "Unable to cast HostDescription to Host.");
  ENVOY_LOG(trace,
            "LoadBalancerContext::OrcaLoadReportCb "
            "orca_load_report for {} report = {}",
            getHostAddress(host), orca_load_report.DebugString());
  auto client_side_data = host->typedLbPolicyData<ClientSideHostLbPolicyData>();
  if (!client_side_data.has_value()) {
    return absl::NotFoundError("Host does not have ClientSideLbPolicyData");
  }
  return updateClientSideDataFromOrcaLoadReport(orca_load_report, *client_side_data);
}

void ClientSideWeightedRoundRobinLoadBalancer::initFromConfig(
    const ClientSideWeightedRoundRobinLbConfig& lb_config) {
  blackout_period_ = lb_config.blackout_period;
  weight_expiration_period_ = lb_config.weight_expiration_period;
  weight_update_period_ = lb_config.weight_update_period;
}

void ClientSideWeightedRoundRobinLoadBalancer::startWeightUpdatesOnMainThread(
    Event::Dispatcher& main_thread_dispatcher) {
  weight_calculation_timer_ = main_thread_dispatcher.createTimer([this]() -> void {
    updateWeightsOnMainThread();
    weight_calculation_timer_->enableTimer(weight_update_period_);
  });
  weight_calculation_timer_->enableTimer(weight_update_period_);
}

void ClientSideWeightedRoundRobinLoadBalancer::updateWeightsOnMainThread() {
  ENVOY_LOG(trace, "updateWeightsOnMainThread");
  for (const HostSetPtr& host_set : priority_set_.hostSetsPerPriority()) {
    updateWeightsOnHosts(host_set->hosts());
  }
}

void ClientSideWeightedRoundRobinLoadBalancer::updateWeightsOnHosts(const HostVector& hosts) {
  std::vector<uint32_t> weights;
  HostVector hosts_with_default_weight;
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
      weights.push_back(*client_side_weight);
      host_ptr->weight(*client_side_weight);
      ENVOY_LOG(trace, "updateWeights hostWeight {} = {}", getHostAddress(host_ptr.get()),
                host_ptr->weight());
    } else {
      // If `client_side_weight` is invalid, then set host to default (median) weight.
      hosts_with_default_weight.push_back(host_ptr);
    }
  }
  // Calculate the default weight as median of all valid weights.
  uint32_t default_weight = 1;
  if (!weights.empty()) {
    auto median_it = weights.begin() + weights.size() / 2;
    std::nth_element(weights.begin(), median_it, weights.end());
    default_weight = *median_it;
  }
  // Update the hosts with default weight.
  for (const auto& host_ptr : hosts_with_default_weight) {
    host_ptr->weight(default_weight);
    ENVOY_LOG(trace, "updateWeights default hostWeight {} = {}", getHostAddress(host_ptr.get()),
              host_ptr->weight());
  }
}

void ClientSideWeightedRoundRobinLoadBalancer::addClientSideLbPolicyDataToHosts(
    const HostVector& hosts) {
  for (const auto& host_ptr : hosts) {
    if (!host_ptr->lbPolicyData().has_value()) {
      ENVOY_LOG(trace, "Adding LB policy data to Host {}", getHostAddress(host_ptr.get()));
      host_ptr->setLbPolicyData(std::make_unique<ClientSideHostLbPolicyData>());
    }
  }
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
  const auto* typed_lb_config =
      dynamic_cast<const ClientSideWeightedRoundRobinLbConfig*>(lb_config_.ptr());
  ASSERT(typed_lb_config != nullptr);
  return std::make_unique<Upstream::ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb>(
      params.priority_set, params.local_priority_set, cluster_info_.lbStats(), runtime_, random_,
      cluster_info_.lbConfig(), *typed_lb_config, time_source_);
}

ClientSideWeightedRoundRobinLoadBalancer::ClientSideWeightedRoundRobinLoadBalancer(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source)
    : factory_(std::make_shared<WorkerLocalLbFactory>(lb_config, cluster_info, priority_set,
                                                      runtime, random, time_source)),
      lb_config_(lb_config), cluster_info_(cluster_info), priority_set_(priority_set),
      runtime_(runtime), random_(random), time_source_(time_source) {}

absl::Status ClientSideWeightedRoundRobinLoadBalancer::initialize() {
  // Ensure that all hosts have client side lb policy data.
  for (const HostSetPtr& host_set : priority_set_.hostSetsPerPriority()) {
    addClientSideLbPolicyDataToHosts(host_set->hosts());
  }
  // Setup a callback to receive priority set updates.
  priority_update_cb_ = priority_set_.addPriorityUpdateCb(
      [](uint32_t, const HostVector& hosts_added, const HostVector&) -> absl::Status {
        addClientSideLbPolicyDataToHosts(hosts_added);
        return absl::OkStatus();
      });

  const auto* typed_lb_config =
      dynamic_cast<const ClientSideWeightedRoundRobinLbConfig*>(lb_config_.ptr());
  ASSERT(typed_lb_config != nullptr);
  initFromConfig(*typed_lb_config);
  startWeightUpdatesOnMainThread(typed_lb_config->main_thread_dispatcher_);
  return absl::OkStatus();
}

} // namespace Upstream
} // namespace Envoy
