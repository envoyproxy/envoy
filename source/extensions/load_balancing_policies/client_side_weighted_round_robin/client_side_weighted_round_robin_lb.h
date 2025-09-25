#pragma once

#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/callback_impl.h"
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
                                       Event::Dispatcher& main_thread_dispatcher,
                                       ThreadLocal::SlotAllocator& tls_slot_allocator);

  // Parameters for weight calculation from Orca Load report.
  std::vector<std::string> metric_names_for_computing_utilization;
  double error_utilization_penalty;
  // Timing parameters for the weight update.
  std::chrono::milliseconds blackout_period;
  std::chrono::milliseconds weight_expiration_period;
  std::chrono::milliseconds weight_update_period;

  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::SlotAllocator& tls_slot_allocator_;
};

/**
 * A client side weighted round robin load balancer. When in weighted mode, EDF
 * scheduling is used. When in not weighted mode, simple RR index selection is
 * used.
 */
class ClientSideWeightedRoundRobinLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                                                 protected Logger::Loggable<Logger::Id::upstream> {
public:
  class OrcaLoadReportHandler;
  using OrcaLoadReportHandlerSharedPtr = std::shared_ptr<OrcaLoadReportHandler>;

  // This struct is used to store the client side data for the host. Hosts are
  // not shared between different clusters, but are shared between load
  // balancer instances on different threads.
  struct ClientSideHostLbPolicyData : public Envoy::Upstream::HostLbPolicyData {
    ClientSideHostLbPolicyData(OrcaLoadReportHandlerSharedPtr handler)
        : report_handler_(std::move(handler)) {}
    ClientSideHostLbPolicyData(OrcaLoadReportHandlerSharedPtr handler, uint32_t weight,
                               MonotonicTime non_empty_since, MonotonicTime last_update_time)
        : report_handler_(std::move(handler)), weight_(weight), non_empty_since_(non_empty_since),
          last_update_time_(last_update_time) {}

    absl::Status onOrcaLoadReport(const Upstream::OrcaLoadReport& report) override;

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

  // This class is used to handle ORCA load reports.
  // It stores the config necessary to calculate host weight based on the report.
  class OrcaLoadReportHandler {
  public:
    OrcaLoadReportHandler(const ClientSideWeightedRoundRobinLbConfig& lb_config,
                          TimeSource& time_source);

    // Update client side data from `orca_load_report`. Invoked from `onOrcaLoadReport` callback of
    // ClientSideHostLbPolicyData.
    absl::Status
    updateClientSideDataFromOrcaLoadReport(const OrcaLoadReportProto& orca_load_report,
                                           ClientSideHostLbPolicyData& client_side_data);

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

    const std::vector<std::string> metric_names_for_computing_utilization_;
    const double error_utilization_penalty_;
    TimeSource& time_source_;
  };

  // Thread local shim to store callbacks for weight updates of worker local lb.
  class ThreadLocalShim : public Envoy::ThreadLocal::ThreadLocalObject {
  public:
    Common::CallbackManager<void, uint32_t> apply_weights_cb_helper_;
  };

  // This class is used to handle the load balancing on the worker thread.
  class WorkerLocalLb : public RoundRobinLoadBalancer {
  public:
    WorkerLocalLb(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                  ClusterLbStats& stats, Runtime::Loader& runtime, Random::RandomGenerator& random,
                  const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
                  TimeSource& time_source, OptRef<ThreadLocalShim> tls_shim);

  private:
    friend class ClientSideWeightedRoundRobinLoadBalancerFriend;
    Common::CallbackHandlePtr apply_weights_cb_handle_;
  };

  // Factory used to create worker-local load balancer on the worker thread.
  class WorkerLocalLbFactory : public Upstream::LoadBalancerFactory {
  public:
    WorkerLocalLbFactory(const Upstream::ClusterInfo& cluster_info,
                         const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                         Envoy::Random::RandomGenerator& random, TimeSource& time_source,
                         ThreadLocal::SlotAllocator& tls)
        : cluster_info_(cluster_info), priority_set_(priority_set), runtime_(runtime),
          random_(random), time_source_(time_source) {
      tls_ = ThreadLocal::TypedSlot<ThreadLocalShim>::makeUnique(tls);
      tls_->set([](Envoy::Event::Dispatcher&) { return std::make_shared<ThreadLocalShim>(); });
    }

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;

    bool recreateOnHostChange() const override { return false; }

    void applyWeightsToAllWorkers(uint32_t priority);

    std::unique_ptr<Envoy::ThreadLocal::TypedSlot<ThreadLocalShim>> tls_;

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

  // Update weights using client side host LB policy data for all priority sets.
  // Executed on the main thread.
  void updateWeightsOnMainThread();

  // Update weights using client side host LB policy data for all `hosts`.
  // Returns true if any host weight is updated.
  bool updateWeightsOnHosts(const HostVector& hosts);

  // Add client side host LB policy data to all `hosts`.
  void addClientSideLbPolicyDataToHosts(const HostVector& hosts);

  // Get weight based on client side host LB policy data if it is valid (not
  // empty at least since `max_non_empty_since` and updated no later than
  // `min_last_update_time`), otherwise return std::nullopt.
  static absl::optional<uint32_t>
  getClientSideWeightIfValidFromHost(const Host& host, MonotonicTime max_non_empty_since,
                                     MonotonicTime min_last_update_time);

  OrcaLoadReportHandlerSharedPtr report_handler_;
  // Factory used to create worker-local load balancers on the worker thread.
  std::shared_ptr<WorkerLocalLbFactory> factory_;

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
