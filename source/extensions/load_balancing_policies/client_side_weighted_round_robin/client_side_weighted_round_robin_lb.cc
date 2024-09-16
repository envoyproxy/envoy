#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>

#include "envoy/common/time.h"
#include "envoy/upstream/upstream.h"

#include "source/common/orca/orca_load_metrics.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/status/status.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

using Envoy::MonotonicTime;
using Envoy::Upstream::Host;

#if TEST_THREAD_SUPPORTED
#define IS_MAIN_OR_TEST_THREAD() (Envoy::Thread::MainThread::isMainOrTestThread())
#else // !TEST_THREAD_SUPPORTED -- just check for the main thread.
#define IS_MAIN_OR_TEST_THREAD() (Envoy::Thread::MainThread::isMainThread())
#endif // TEST_THREAD_SUPPORTED

namespace {

std::string getHostAddress(const Host* host) {
  if (host == nullptr || host->address() == nullptr) {
    return "unknown";
  }
  return host->address()->asString();
}

} // namespace

namespace Envoy {
namespace Upstream {

ClientSideWeightedRoundRobinLoadBalancer::ClientSideWeightedRoundRobinLoadBalancer(
    const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
    Runtime::Loader& runtime, Random::RandomGenerator& random,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
    const envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
        ClientSideWeightedRoundRobin& client_side_weighted_round_robin_config,
    TimeSource& time_source, Event::Dispatcher& main_thread_dispatcher)
    : EdfLoadBalancerBase(
          priority_set, local_priority_set, stats, runtime, random,
          PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(common_config, healthy_panic_threshold,
                                                         100, 50),
          LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(common_config),
          /*slow_start_config=*/std::nullopt, time_source) {
  ENVOY_LOG(trace, "RoundRobinLbConfig config {} {}", reinterpret_cast<int64_t>(this),
            client_side_weighted_round_robin_config.DebugString());
  initialize();
  initFromConfig(client_side_weighted_round_robin_config);
  if (IS_MAIN_OR_TEST_THREAD()) {
    startWeightUpdatesOnMainThread(main_thread_dispatcher);
  }
}

// {LoadBalancer} Interface implementation.
void ClientSideWeightedRoundRobinLoadBalancer::refreshHostSource(const HostsSource& source) {
  // insert() is used here on purpose so that we don't overwrite the index if
  // the host source already exists. Note that host sources will never be
  // removed, but given how uncommon this is it probably doesn't matter.
  rr_indexes_.insert({source, seed_});
  // If the list of hosts changes, the order of picks change. Discard the
  // index.
  peekahead_index_ = 0;

  if (!IS_MAIN_OR_TEST_THREAD()) {
    return;
  }

  // On the main thread ensure that all hosts have client side lb policy data.
  addClientSideLbPolicyDataToHosts(hostSourceToHosts(source));
}

HostConstSharedPtr
ClientSideWeightedRoundRobinLoadBalancer::chooseHost(LoadBalancerContext* context) {
  HostConstSharedPtr host = EdfLoadBalancerBase::chooseHost(context);
  if (context != nullptr) {
    // Configure callbacks to receive ORCA load reports.
    context->setOrcaLoadReportCallbacks(*this);
  }
  return host;
}

double ClientSideWeightedRoundRobinLoadBalancer::hostWeight(const Host& host) const {
  ENVOY_LOG(trace, "hostWeight {} = {}", getHostAddress(&host), host.weight());
  return host.weight();
}

HostConstSharedPtr
ClientSideWeightedRoundRobinLoadBalancer::unweightedHostPeek(const HostVector& hosts_to_use,
                                                             const HostsSource& source) {
  auto i = rr_indexes_.find(source);
  if (i == rr_indexes_.end()) {
    return nullptr;
  }
  return hosts_to_use[(i->second + (peekahead_index_)++) % hosts_to_use.size()];
}

HostConstSharedPtr
ClientSideWeightedRoundRobinLoadBalancer::unweightedHostPick(const HostVector& hosts_to_use,
                                                             const HostsSource& source) {
  if (peekahead_index_ > 0) {
    --peekahead_index_;
  }
  // To avoid storing the RR index in the base class, we end up using a second
  // map here with host source as the key. This means that each LB decision
  // will require two map lookups in the unweighted case. We might consider
  // trying to optimize this in the future.
  ASSERT(rr_indexes_.find(source) != rr_indexes_.end());
  return hosts_to_use[rr_indexes_[source]++ % hosts_to_use.size()];
}

absl::Status ClientSideWeightedRoundRobinLoadBalancer::onOrcaLoadReport(
    const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
    const HostDescription& host_description) {
  const Host* host = dynamic_cast<const Host*>(&host_description);
  ENVOY_BUG(host != nullptr, "Unable to cast HostDescription to Host.");
  ENVOY_LOG(trace,
            "LoadBalancerContext::OrcaLoadReportCb "
            "orca_load_report for {} report = {}",
            getHostAddress(host), orca_load_report.DebugString());
  const auto& lb_policy_data_ptr = host->lbPolicyData();
  auto* client_side_data = dynamic_cast<ClientSideHostLbPolicyData*>(lb_policy_data_ptr.get());
  ENVOY_BUG(client_side_data != nullptr,
            "Unable to cast Host::lpPolicyData to ClientSideHostLbPolicyData.");
  if (client_side_data == nullptr) {
    return absl::NotFoundError("Host does not have ClientSideLbPolicyData");
  }
  return updateClientSideDataFromOrcaLoadReport(orca_load_report, *client_side_data);
}

void ClientSideWeightedRoundRobinLoadBalancer::initFromConfig(
    const envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
        ClientSideWeightedRoundRobin& client_side_weighted_round_robin_config) {
  metric_names_for_computing_utilization_ = std::vector<std::string>(
      client_side_weighted_round_robin_config.metric_names_for_computing_utilization().begin(),
      client_side_weighted_round_robin_config.metric_names_for_computing_utilization().end());
  error_utilization_penalty_ =
      client_side_weighted_round_robin_config.error_utilization_penalty().value();
  blackout_period_ = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(client_side_weighted_round_robin_config, blackout_period, 10000));
  weight_expiration_period_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
      client_side_weighted_round_robin_config, weight_expiration_period, 180000));
  weight_update_period_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
      client_side_weighted_round_robin_config, weight_update_period, 1000));
}

void ClientSideWeightedRoundRobinLoadBalancer::startWeightUpdatesOnMainThread(
    Event::Dispatcher& main_thread_dispatcher) {
  if (!IS_MAIN_OR_TEST_THREAD()) {
    return;
  }
  weight_calculation_timer_ = main_thread_dispatcher.createTimer([this]() -> void {
    updateWeightsOnMainThread();
    weight_calculation_timer_->enableTimer(weight_update_period_);
  });
  weight_calculation_timer_->enableTimer(weight_update_period_);
}

void ClientSideWeightedRoundRobinLoadBalancer::updateWeightsOnMainThread() {
  ENVOY_LOG(trace, "updateWeightsOnMainThread");
  ENVOY_BUG(IS_MAIN_OR_TEST_THREAD(), "Update Weights NOT on MainThread");
  for (uint32_t priority = 0; priority < priority_set_.hostSetsPerPriority().size(); ++priority) {
    HostsSource source(priority, HostsSource::SourceType::AllHosts);
    updateWeightsOnHosts(hostSourceToHosts(source));
  }
}

void ClientSideWeightedRoundRobinLoadBalancer::updateWeightsOnHosts(const HostVector& hosts) {
  std::vector<uint32_t> weights;
  HostVector hosts_with_default_weight;
  const MonotonicTime now = time_source_.monotonicTime();
  const MonotonicTime min_non_empty_since = now - blackout_period_;
  const MonotonicTime max_last_update_time = now - weight_expiration_period_;
  weights.reserve(hosts.size());
  hosts_with_default_weight.reserve(hosts.size());
  ENVOY_LOG(trace, "updateWeights hosts.size() = {}, time since epoch = {}", hosts.size(),
            now.time_since_epoch().count());
  // Scan through all hosts and update their weights if they are valid.
  for (const auto& host_ptr : hosts) {
    absl::optional<uint32_t> client_side_weight =
        getClientSideWeightIfValidFromHost(*host_ptr, min_non_empty_since, max_last_update_time);
    if (client_side_weight.has_value()) {
      weights.push_back(*client_side_weight);
      host_ptr->weight(*client_side_weight);
      ENVOY_LOG(trace, "updateWeights hostWeight {} = {}", getHostAddress(host_ptr.get()),
                host_ptr->weight());
    } else {
      hosts_with_default_weight.push_back(host_ptr);
    }
  }
  // Calculate the default weight as median of all weights.
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
    if (host_ptr->lbPolicyData() == nullptr) {
      ENVOY_LOG(trace, "Adding LB policy data to Host {}", getHostAddress(host_ptr.get()));
      host_ptr->setLbPolicyData(std::make_shared<ClientSideHostLbPolicyData>());
    }
  }
}

absl::optional<uint32_t>
ClientSideWeightedRoundRobinLoadBalancer::getClientSideWeightIfValidFromHost(
    const Host& host, const MonotonicTime& min_non_empty_since,
    const MonotonicTime& max_last_update_time) {
  const auto& lb_policy_data_ptr = host.lbPolicyData();
  auto* client_side_data = dynamic_cast<ClientSideHostLbPolicyData*>(lb_policy_data_ptr.get());
  if (client_side_data == nullptr) {
    ENVOY_LOG(trace, "Host does not have ClientSideHostLbPolicyData {}", getHostAddress(&host));
    return std::nullopt;
  }
  // Get the weight based on the client side data if it is not expired.
  absl::MutexLock lock(&client_side_data->mu_);
  // If non_empty_since_ is too recent, we should use the default weight.
  if (client_side_data->non_empty_since_ > min_non_empty_since) {
    ENVOY_LOG(error,
              "Host {} ClientSideHostLbPolicyData non_empty_since_ is too "
              "recent: {} > {}",
              getHostAddress(&host), client_side_data->non_empty_since_.time_since_epoch().count(),
              min_non_empty_since.time_since_epoch().count());
    return std::nullopt;
  }
  // If last update time is too old, we should use the default weight.
  if (client_side_data->last_update_time_ < max_last_update_time) {
    ENVOY_LOG(trace, "Host {} ClientSideHostLbPolicyData last_update_time_ is too old",
              getHostAddress(&host));
    // Also reset the non_empty_since_ time so the timer will start again.
    client_side_data->non_empty_since_ = ClientSideHostLbPolicyData::kDefaultNonEmptySince;
    return std::nullopt;
  }
  return client_side_data->weight_;
}

double ClientSideWeightedRoundRobinLoadBalancer::getUtilizationFromOrcaReport(
    const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
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

absl::StatusOr<uint32_t> ClientSideWeightedRoundRobinLoadBalancer::calculateWeightFromOrcaReport(
    const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
    const std::vector<std::string>& metric_names_for_computing_utilization,
    double error_utilization_penalty) {
  double qps = orca_load_report.rps_fractional();
  if (qps <= 0) {
    return absl::InvalidArgumentError("QPS must be positive");
  }

  double utilization =
      getUtilizationFromOrcaReport(orca_load_report, metric_names_for_computing_utilization);
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

absl::Status ClientSideWeightedRoundRobinLoadBalancer::updateClientSideDataFromOrcaLoadReport(
    const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
    ClientSideHostLbPolicyData& client_side_data) {
  const absl::StatusOr<uint32_t> weight = calculateWeightFromOrcaReport(
      orca_load_report, metric_names_for_computing_utilization_, error_utilization_penalty_);
  if (!weight.ok()) {
    return weight.status();
  }

  const MonotonicTime now = time_source_.monotonicTime();
  // Update client side data attached to the host.
  absl::MutexLock lock(&client_side_data.mu_);
  client_side_data.weight_ = weight.value();
  client_side_data.last_update_time_ = now;
  if (client_side_data.non_empty_since_ == ClientSideHostLbPolicyData::kDefaultNonEmptySince) {
    client_side_data.non_empty_since_ = now;
  }
  return absl::OkStatus();
}

} // namespace Upstream
} // namespace Envoy
