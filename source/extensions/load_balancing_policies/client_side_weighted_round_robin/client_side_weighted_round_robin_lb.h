#pragma once

#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Upstream {

/**
 * A client side weighted round robin load balancer. When in weighted mode, EDF
 * scheduling is used. When in not weighted mode, simple RR index selection is
 * used.
 */
class ClientSideWeightedRoundRobinLoadBalancer : public EdfLoadBalancerBase {
public:
  // This struct is used to store the client side data for the host. Hosts are
  // not shared between different clusters, but are shared between load
  // balancer instances on different threads.
  struct ClientSideHostLbPolicyData : public Envoy::Upstream::Host::HostLbPolicyData {
    ClientSideHostLbPolicyData() = default;
    ClientSideHostLbPolicyData(uint32_t weight, MonotonicTime non_empty_since,
                               MonotonicTime last_update_time)
        : weight_(weight), non_empty_since_(non_empty_since), last_update_time_(last_update_time) {}
    virtual ~ClientSideHostLbPolicyData() = default;

    absl::Mutex mu_;
    uint32_t weight_ ABSL_GUARDED_BY(mu_) = 1;
    MonotonicTime non_empty_since_ ABSL_GUARDED_BY(&mu_) = kDefaultNonEmptySince;
    MonotonicTime last_update_time_ ABSL_GUARDED_BY(&mu_) = kDefaultLastUpdateTime;

    static constexpr MonotonicTime kDefaultNonEmptySince = MonotonicTime::max();
    static constexpr MonotonicTime kDefaultLastUpdateTime = MonotonicTime::min();
  };

  // This class is used to handle ORCA load reports.
  // It stores the config necessary to calculate host weight based on the report.
  // The load balancer context stores a weak pointer to this handler,
  // so it is NOT invoked if the load balancer is deleted.
  class OrcaLoadReportHandler : public LoadBalancerContext::OrcaLoadReportCallbacks {
  public:
    OrcaLoadReportHandler(
        const envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
            ClientSideWeightedRoundRobin& client_side_weighted_round_robin_config,
        TimeSource& time_source);
    ~OrcaLoadReportHandler() override = default;

  private:
    friend class ClientSideWeightedRoundRobinLoadBalancerFriend;

    // {LoadBalancerContext::OrcaLoadReportCallbacks} implementation.
    absl::Status onOrcaLoadReport(const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
                                  const HostDescription& host_description) override;

    // Get utilization from `orca_load_report` using named metrics specified in
    // `metric_names_for_computing_utilization`.
    static double getUtilizationFromOrcaReport(
        const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
        const std::vector<std::string>& metric_names_for_computing_utilization);

    // Calculate client side weight from `orca_load_report` using `getUtilizationFromOrcaReport()`,
    // QPS, EPS and `error_utilization_penalty`.
    static absl::StatusOr<uint32_t> calculateWeightFromOrcaReport(
        const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
        const std::vector<std::string>& metric_names_for_computing_utilization,
        double error_utilization_penalty);

    // Update client side data from `orca_load_report`. Invoked from `onOrcaLoadReport` callback on
    // the worker thread.
    absl::Status updateClientSideDataFromOrcaLoadReport(
        const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
        ClientSideHostLbPolicyData& client_side_data);

    std::vector<std::string> metric_names_for_computing_utilization_;
    double error_utilization_penalty_;
    TimeSource& time_source_;
  };

  ClientSideWeightedRoundRobinLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
      const envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
          ClientSideWeightedRoundRobin& client_side_weighted_round_robin_config,
      TimeSource& time_source, Event::Dispatcher& main_thread_dispatcher);

private:
  friend class ClientSideWeightedRoundRobinLoadBalancerFriend;

  // {LoadBalancer} Interface implementation.
  void refreshHostSource(const HostsSource& source) override;

  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

  double hostWeight(const Host& host) const override;
  HostConstSharedPtr unweightedHostPeek(const HostVector& hosts_to_use,
                                        const HostsSource& source) override;

  HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                        const HostsSource& source) override;

  // Initialize LB policy based on the config.
  void initFromConfig(
      const envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
          ClientSideWeightedRoundRobin& client_side_weighted_round_robin_config);

  // Start weight updates on main thread only.
  void startWeightUpdatesOnMainThread(Event::Dispatcher& main_thread_dispatcher);

  // Update weights using client side host LB policy data for all priority sets.
  // Executed on the main thread.
  void updateWeightsOnMainThread();

  // Update weights using client side host LB policy data for all `hosts`.
  void updateWeightsOnHosts(const HostVector& hosts);

  // Add client side host LB policy data to all `hosts`.
  static void addClientSideLbPolicyDataToHosts(const HostVector& hosts);

  // Get weight based on client side host LB policy data if it is valid (not
  // empty at least since `min_non_empty_since` and updated no later than
  // `max_last_update_time`), otherwise return std::nullopt.
  static absl::optional<uint32_t>
  getClientSideWeightIfValidFromHost(const Host& host, const MonotonicTime& min_non_empty_since,
                                     const MonotonicTime& max_last_update_time);

  uint64_t peekahead_index_{};
  absl::flat_hash_map<HostsSource, uint64_t, HostsSourceHash> rr_indexes_;
  std::shared_ptr<OrcaLoadReportHandler> orca_load_report_handler_;
  // Timing parameters for the weight update.
  std::chrono::milliseconds blackout_period_;
  std::chrono::milliseconds weight_expiration_period_;
  std::chrono::milliseconds weight_update_period_;

  Event::TimerPtr weight_calculation_timer_;
};

} // namespace Upstream
} // namespace Envoy
