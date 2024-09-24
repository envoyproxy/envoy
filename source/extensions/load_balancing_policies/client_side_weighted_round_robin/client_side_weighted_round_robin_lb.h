#pragma once

#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"
#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Upstream {

using ClientSideWeightedRoundRobinLbProto = envoy::extensions::load_balancing_policies::
    client_side_weighted_round_robin::v3::ClientSideWeightedRoundRobin;
using OrcaLoadReportProto = xds::data::orca::v3::OrcaLoadReport;

/**
 * Load balancer config used to wrap the config proto.
 */
class ClientSideWeightedRoundRobinLbConfig : public Upstream::LoadBalancerConfig {
public:
  ClientSideWeightedRoundRobinLbConfig(const ClientSideWeightedRoundRobinLbProto& lb_proto,
                                       Event::Dispatcher& main_thread_dispatcher);

  // Parameters for weight calculation from Orca Load report.
  std::vector<std::string> metric_names_for_computing_utilization;
  double error_utilization_penalty;
  // Timing parameters for the weight update.
  std::chrono::milliseconds blackout_period;
  std::chrono::milliseconds weight_expiration_period;
  std::chrono::milliseconds weight_update_period;

  Event::Dispatcher& main_thread_dispatcher_;
};

/**
 * A client side weighted round robin load balancer. When in weighted mode, EDF
 * scheduling is used. When in not weighted mode, simple RR index selection is
 * used.
 */
class ClientSideWeightedRoundRobinLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                                                 protected Logger::Loggable<Logger::Id::upstream> {
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
        return std::nullopt;
      }
      // If last update time is too old, we should use the default weight.
      if (last_update_time_.load() < min_last_update_time) {
        // Reset the non_empty_since_ time so the timer will start again.
        non_empty_since_.store(ClientSideHostLbPolicyData::kDefaultNonEmptySince);
        return std::nullopt;
      }
      return weight_;
    }

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

  // This class is used to handle ORCA load reports.
  // It stores the config necessary to calculate host weight based on the report.
  // The load balancer context stores a weak pointer to this handler,
  // so it is NOT invoked if the load balancer is deleted.
  class OrcaLoadReportHandler : public LoadBalancerContext::OrcaLoadReportCallbacks {
  public:
    OrcaLoadReportHandler(const ClientSideWeightedRoundRobinLbConfig& lb_config,
                          TimeSource& time_source);
    ~OrcaLoadReportHandler() override = default;

  private:
    friend class ClientSideWeightedRoundRobinLoadBalancerFriend;

    // {LoadBalancerContext::OrcaLoadReportCallbacks} implementation.
    absl::Status onOrcaLoadReport(const OrcaLoadReportProto& orca_load_report,
                                  const HostDescription& host_description) override;

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

    // Update client side data from `orca_load_report`. Invoked from `onOrcaLoadReport` callback on
    // the worker thread.
    absl::Status
    updateClientSideDataFromOrcaLoadReport(const OrcaLoadReportProto& orca_load_report,
                                           ClientSideHostLbPolicyData& client_side_data);

    const std::vector<std::string> metric_names_for_computing_utilization_;
    const double error_utilization_penalty_;
    TimeSource& time_source_;
  };

  // This class is used to handle the load balancing on the worker thread.
  class WorkerLocalLb : public RoundRobinLoadBalancer {
  public:
    WorkerLocalLb(
        const PrioritySet& priority_set, const PrioritySet* local_priority_set,
        ClusterLbStats& stats, Runtime::Loader& runtime, Random::RandomGenerator& random,
        const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
        const ClientSideWeightedRoundRobinLbConfig& client_side_weighted_round_robin_config,
        TimeSource& time_source);

  private:
    friend class ClientSideWeightedRoundRobinLoadBalancerFriend;

    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;
    bool alwaysUseEdfScheduler() const override { return true; };

    std::shared_ptr<OrcaLoadReportHandler> orca_load_report_handler_;
  };

  // Factory used to create worker-local load balancer on the worker thread.
  class WorkerLocalLbFactory : public Upstream::LoadBalancerFactory {
  public:
    WorkerLocalLbFactory(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                         const Upstream::ClusterInfo& cluster_info,
                         const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                         Envoy::Random::RandomGenerator& random, TimeSource& time_source)
        : lb_config_(lb_config), cluster_info_(cluster_info), priority_set_(priority_set),
          runtime_(runtime), random_(random), time_source_(time_source) {}

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;

    bool recreateOnHostChange() const override { return false; }

  protected:
    OptRef<const Upstream::LoadBalancerConfig> lb_config_;

    const Upstream::ClusterInfo& cluster_info_;
    const Upstream::PrioritySet& priority_set_;
    Runtime::Loader& runtime_;
    Envoy::Random::RandomGenerator& random_;
    TimeSource& time_source_;
  };

public:
  ClientSideWeightedRoundRobinLoadBalancer(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                           const Upstream::ClusterInfo& cluster_info,
                                           const Upstream::PrioritySet& priority_set,
                                           Runtime::Loader& runtime,
                                           Envoy::Random::RandomGenerator& random,
                                           TimeSource& time_source);

private:
  friend class ClientSideWeightedRoundRobinLoadBalancerFriend;

  // {Upstream::ThreadAwareLoadBalancer} Interface implementation.
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override;

  // Initialize LB based on the config.
  void initFromConfig(const ClientSideWeightedRoundRobinLbConfig& lb_config);

  // Start weight updates on the main thread.
  void startWeightUpdatesOnMainThread(Event::Dispatcher& main_thread_dispatcher);

  // Update weights using client side host LB policy data for all priority sets.
  // Executed on the main thread.
  void updateWeightsOnMainThread();

  // Update weights using client side host LB policy data for all `hosts`.
  void updateWeightsOnHosts(const HostVector& hosts);

  // Add client side host LB policy data to all `hosts`.
  static void addClientSideLbPolicyDataToHosts(const HostVector& hosts);

  // Get weight based on client side host LB policy data if it is valid (not
  // empty at least since `max_non_empty_since` and updated no later than
  // `min_last_update_time`), otherwise return std::nullopt.
  static absl::optional<uint32_t>
  getClientSideWeightIfValidFromHost(const Host& host, MonotonicTime max_non_empty_since,
                                     MonotonicTime min_last_update_time);

  // Factory used to create worker-local load balancers on the worker thread.
  std::shared_ptr<WorkerLocalLbFactory> factory_;
  // Data that is also passed to the worker-local load balancer via factory_.
  OptRef<const Upstream::LoadBalancerConfig> lb_config_;
  const Upstream::ClusterInfo& cluster_info_;
  const Upstream::PrioritySet& priority_set_;
  Runtime::Loader& runtime_;
  Envoy::Random::RandomGenerator& random_;
  TimeSource& time_source_;

  // Timing parameters for the weight update.
  std::chrono::milliseconds blackout_period_;
  std::chrono::milliseconds weight_expiration_period_;
  std::chrono::milliseconds weight_update_period_;

  Event::TimerPtr weight_calculation_timer_;
  // Callback for `priority_set_` updates.
  Common::CallbackHandlePtr priority_update_cb_;
};

} // namespace Upstream
} // namespace Envoy
