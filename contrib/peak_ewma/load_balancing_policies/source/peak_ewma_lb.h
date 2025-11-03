#pragma once

#include <atomic>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/thread.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/envoy/extensions/load_balancing_policies/peak_ewma/v3alpha/peak_ewma.pb.h"
#include "contrib/peak_ewma/load_balancing_policies/source/cost.h"
#include "contrib/peak_ewma/load_balancing_policies/source/host_data.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

// Forward declarations and type aliases.
using Config = envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma;

constexpr int64_t kDefaultDecayTimeSeconds = 10;
constexpr double kDefaultRttMilliseconds = 10.0; // Default RTT for new hosts (10ms).

namespace {
class PeakEwmaTestPeer;
} // namespace

class PeakEwmaLoadBalancerFactory;
class PeakEwmaLoadBalancer;
class Cost;

/**
 * Manages stats for a single host, publishing EWMA RTT, active requests, and computed cost
 * to the admin interface for observability.
 */
struct GlobalHostStats {
  GlobalHostStats(Upstream::HostConstSharedPtr host, Stats::Scope& scope);

  void setComputedCostStat(double cost);
  void setEwmaRttStat(double ewma_rtt_ms);
  void setActiveRequestsStat(double active_requests);

private:
  Stats::Gauge& cost_stat_;
  Stats::Gauge& ewma_rtt_stat_;
  Stats::Gauge& active_requests_stat_;
  Upstream::HostConstSharedPtr host_;
};

/**
 * Peak EWMA Load Balancer Implementation.
 *
 * Uses host-attached atomic ring buffers for RTT sample storage. Worker threads
 * record RTT samples directly into host objects. Main thread periodically processes
 * these samples to update EWMA values. Load balancing uses Power of Two Choices
 * algorithm with latency-aware cost function.
 *
 * Architecture:
 * - HTTP filter records RTT samples in host-attached ring buffers (lock-free)
 * - Timer aggregates samples every 100ms and updates EWMA values
 * - P2C selection uses current EWMA + active requests for cost calculation
 */
class PeakEwmaLoadBalancer : public Upstream::LoadBalancerBase {
public:
  PeakEwmaLoadBalancer(
      const Upstream::PrioritySet& priority_set, const Upstream::PrioritySet* local_priority_set,
      Upstream::ClusterLbStats& stats, Runtime::Loader& runtime, Random::RandomGenerator& random,
      uint32_t healthy_panic_threshold, const Upstream::ClusterInfo& cluster_info,
      TimeSource& time_source,
      const envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma& config,
      Event::Dispatcher& main_dispatcher);

  ~PeakEwmaLoadBalancer();

  // LoadBalancer interface
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;

private:
  friend struct GlobalHostStats;

  // Host management - attach atomic data structures to each host.
  void addPeakEwmaLbPolicyDataToHosts(const Upstream::HostVector& hosts);
  PeakEwmaHostLbPolicyData* getPeakEwmaData(Upstream::HostConstSharedPtr host);

  // Timer-based aggregation - processes host-attached sample data.
  void aggregateWorkerData();
  void processHostSamples(Upstream::HostConstSharedPtr host, PeakEwmaHostLbPolicyData* data);
  void onAggregationTimer();

  // Power of Two Choices selection.
  Upstream::HostConstSharedPtr selectFromTwoCandidates(const Upstream::HostVector& hosts,
                                                       uint64_t random_value);
  double calculateHostCost(Upstream::HostConstSharedPtr host);

  // EWMA calculation helpers.
  size_t calculateNewSampleCount(size_t last_processed, size_t current_write, size_t max_samples);
  double calculateTimeBasedAlpha(uint64_t current_time_ns, uint64_t sample_time_ns);
  double updateEwmaWithSample(double current_ewma, double new_rtt_ms, double alpha);

  // Core infrastructure.
  const Upstream::PrioritySet& priority_set_;
  const envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma config_proto_;
  Random::RandomGenerator& random_;
  TimeSource& time_source_;
  Stats::Scope& stats_scope_;

  // Business logic components.
  Cost cost_;

  // Timer infrastructure for periodic EWMA calculation.
  Event::Dispatcher& main_dispatcher_;
  Event::TimerPtr aggregation_timer_;
  const std::chrono::milliseconds aggregation_interval_;

  // Host stats for admin interface visibility.
  absl::flat_hash_map<Upstream::HostConstSharedPtr, std::unique_ptr<GlobalHostStats>>
      all_host_stats_;

  // Priority set callback for adding atomic data to new hosts.
  ::Envoy::Common::CallbackHandlePtr priority_update_cb_;

  // EWMA calculation constants.
  const int64_t tau_nanos_;  // Decay time in nanoseconds.
  const size_t max_samples_; // Configurable ring buffer size per host.
};

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
