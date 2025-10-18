#include "contrib/peak_ewma/load_balancing_policies/source/peak_ewma_lb.h"

#include <memory>

#include "envoy/common/optref.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "contrib/peak_ewma/load_balancing_policies/source/cost.h"
#include "contrib/peak_ewma/load_balancing_policies/source/host_data.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

// GlobalHostStats implementation.

GlobalHostStats::GlobalHostStats(Upstream::HostConstSharedPtr host, Stats::Scope& scope)
    : cost_stat_(scope.gaugeFromString("peak_ewma." + host->address()->asString() + ".cost",
                                       Stats::Gauge::ImportMode::NeverImport)),
      ewma_rtt_stat_(
          scope.gaugeFromString("peak_ewma." + host->address()->asString() + ".ewma_rtt_ms",
                                Stats::Gauge::ImportMode::NeverImport)),
      active_requests_stat_(
          scope.gaugeFromString("peak_ewma." + host->address()->asString() + ".active_requests",
                                Stats::Gauge::ImportMode::NeverImport)),
      host_(host) {}

void GlobalHostStats::setComputedCostStat(double cost) {
  cost_stat_.set(static_cast<uint64_t>(cost));
}

void GlobalHostStats::setEwmaRttStat(double ewma_rtt_ms) {
  ewma_rtt_stat_.set(static_cast<uint64_t>(ewma_rtt_ms));
}

void GlobalHostStats::setActiveRequestsStat(double active_requests) {
  active_requests_stat_.set(static_cast<uint64_t>(active_requests));
}

// Peak EWMA Load Balancer Implementation.

PeakEwmaLoadBalancer::PeakEwmaLoadBalancer(
    const Upstream::PrioritySet& priority_set, const Upstream::PrioritySet* /*local_priority_set*/,
    Upstream::ClusterLbStats& /*stats*/, Runtime::Loader& runtime, Random::RandomGenerator& random,
    uint32_t /* healthy_panic_threshold */, const Upstream::ClusterInfo& cluster_info,
    TimeSource& time_source,
    const envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma& config,
    Event::Dispatcher& main_dispatcher)
    : LoadBalancerBase(priority_set, cluster_info.lbStats(), runtime, random,
                       PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
                           cluster_info.lbConfig(), healthy_panic_threshold, 100, 50)),
      priority_set_(priority_set), config_proto_(config), random_(random),
      time_source_(time_source), stats_scope_(cluster_info.statsScope()),
      cost_(config.has_penalty_value() ? config.penalty_value().value() : 1000000.0),
      main_dispatcher_(main_dispatcher),
      aggregation_interval_(config_proto_.has_aggregation_interval()
                                ? std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
                                      config_proto_.aggregation_interval()))
                                : std::chrono::milliseconds(100)),
      tau_nanos_(config_proto_.has_decay_time()
                     ? DurationUtil::durationToMilliseconds(config_proto_.decay_time()) * 1000000LL
                     : kDefaultDecayTimeSeconds * 1000000000LL),
      max_samples_(config_proto_.has_max_samples_per_host()
                       ? config_proto_.max_samples_per_host().value()
                       : 1000) {

  // Add PeakEwmaHostLbPolicyData to all existing hosts.
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    addPeakEwmaLbPolicyDataToHosts(host_set->hosts());
  }

  // Setup callback to add data to new hosts.
  priority_update_cb_ =
      priority_set_.addPriorityUpdateCb([this](uint32_t, const Upstream::HostVector& hosts_added,
                                               const Upstream::HostVector&) -> absl::Status {
        addPeakEwmaLbPolicyDataToHosts(hosts_added);
        return absl::OkStatus();
      });

  // Create timer for EWMA aggregation.
  aggregation_timer_ = main_dispatcher_.createTimer([this]() -> void { onAggregationTimer(); });
  aggregation_timer_->enableTimer(aggregation_interval_);

  // Peak EWMA load balancer initialized successfully.
}

PeakEwmaLoadBalancer::~PeakEwmaLoadBalancer() {
  // Post timer cancellation to main thread to avoid cross-thread timer operations.
  // Timer must be disabled from the same thread that created it (main_dispatcher_).
  if (aggregation_timer_) {
    main_dispatcher_.post([timer = std::move(aggregation_timer_)]() mutable {
      if (timer) {
        timer->disableTimer();
        timer.reset();
      }
    });
  }

  // EWMA snapshot cleanup is automatic via shared_ptr destructor.

  // Clean up host data.
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      host->setLbPolicyData(nullptr);
    }
  }
}

// Host management.
void PeakEwmaLoadBalancer::addPeakEwmaLbPolicyDataToHosts(const Upstream::HostVector& hosts) {
  for (const auto& host_ptr : hosts) {
    if (!host_ptr->lbPolicyData().has_value()) {
      host_ptr->setLbPolicyData(std::make_unique<PeakEwmaHostLbPolicyData>(max_samples_));
    }
  }
}

PeakEwmaHostLbPolicyData* PeakEwmaLoadBalancer::getPeakEwmaData(Upstream::HostConstSharedPtr host) {
  auto lb_data = host->lbPolicyData();
  if (!lb_data.has_value()) {
    return nullptr;
  }
  return dynamic_cast<PeakEwmaHostLbPolicyData*>(lb_data.ptr());
}

void PeakEwmaLoadBalancer::onAggregationTimer() {
  // Timer callback - aggregate EWMA data from all hosts.
  aggregateWorkerData();

  // Reschedule timer for next cycle.
  aggregation_timer_->enableTimer(aggregation_interval_);
}

double PeakEwmaLoadBalancer::calculateHostCost(Upstream::HostConstSharedPtr host) {
  // Get EWMA RTT from host-attached atomic data.
  auto* peak_data = getPeakEwmaData(host);
  double ewma_rtt = peak_data ? peak_data->getEwmaRtt() : 0.0;

  // Get active requests from host stats.
  double active_requests = host->stats().rq_active_.value();

  // Calculate cost using business logic.
  double default_rtt_ms = config_proto_.has_default_rtt()
                              ? DurationUtil::durationToMilliseconds(config_proto_.default_rtt())
                              : kDefaultRttMilliseconds;

  return cost_.compute(ewma_rtt, active_requests, default_rtt_ms);
}

Upstream::HostConstSharedPtr
PeakEwmaLoadBalancer::selectFromTwoCandidates(const Upstream::HostVector& hosts,
                                              uint64_t random_value) {

  if (hosts.size() < 2) {
    return hosts.empty() ? nullptr : hosts[0];
  }

  // Generate two distinct host indices using random value.
  const size_t host_count = hosts.size();
  const size_t first_index = random_value % host_count;
  size_t second_index = (random_value >> 16) % host_count;

  // Ensure distinct indices.
  if (second_index == first_index) {
    second_index = (second_index + 1) % host_count;
  }

  auto first_host = hosts[first_index];
  auto second_host = hosts[second_index];

  // Calculate costs using host-attached EWMA data.
  double first_cost = calculateHostCost(first_host);
  double second_cost = calculateHostCost(second_host);

  // Select host with lower cost (tie-breaking with random).
  bool costs_equal = (first_cost == second_cost);
  bool prefer_first = costs_equal ? (random_value & 0x1) != 0 : first_cost < second_cost;

  auto selected_host = prefer_first ? first_host : second_host;

  // Host selection complete.

  return selected_host;
}

Upstream::HostSelectionResponse
PeakEwmaLoadBalancer::chooseHost(Upstream::LoadBalancerContext* /* context */) {
  // Power of Two Choices selection using host-attached EWMA data.
  const auto& host_sets = priority_set_.hostSetsPerPriority();

  if (host_sets.empty()) {
    return {nullptr, ""};
  }

  // Use first priority for now (can be extended for multi-priority).
  const auto& hosts = host_sets[0]->healthyHosts();

  if (hosts.empty()) {
    return {nullptr, ""};
  }

  if (hosts.size() == 1) {
    return {hosts[0], ""};
  }

  // Power of Two Choices selection using host-attached EWMA data.
  uint64_t random_value = random_.random();
  return {selectFromTwoCandidates(hosts, random_value), ""};
}

Upstream::HostConstSharedPtr PeakEwmaLoadBalancer::peekAnotherHost(
    ABSL_ATTRIBUTE_UNUSED Upstream::LoadBalancerContext* context) {
  return nullptr;
}

void PeakEwmaLoadBalancer::aggregateWorkerData() {
  // Process atomic ring buffers attached to each host.

  // Process each host's atomic ring buffer directly (no cross-thread complexity).
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      auto* peak_data = getPeakEwmaData(host);
      if (peak_data) {
        processHostSamples(host, peak_data);
      }
    }
  }

  // Publish stats for admin interface visibility.
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      auto* peak_data = getPeakEwmaData(host);
      if (peak_data) {
        double ewma_rtt = peak_data->getEwmaRtt();
        double active_requests = host->stats().rq_active_.value();
        double cost =
            cost_.compute(ewma_rtt, active_requests,
                          config_proto_.has_default_rtt()
                              ? DurationUtil::durationToMilliseconds(config_proto_.default_rtt())
                              : kDefaultRttMilliseconds);

        // Create stats object if it doesn't exist.
        auto it = all_host_stats_.find(host);
        if (it == all_host_stats_.end()) {
          all_host_stats_[host] = std::make_unique<GlobalHostStats>(host, stats_scope_);
          it = all_host_stats_.find(host);
        }

        // Update stats for observability.
        if (it != all_host_stats_.end()) {
          it->second->setEwmaRttStat(ewma_rtt);
          it->second->setActiveRequestsStat(active_requests);
          it->second->setComputedCostStat(cost);
        }

        // Host processing complete.
      }
    }
  }

  // Aggregation cycle complete.
}

size_t PeakEwmaLoadBalancer::calculateNewSampleCount(size_t last_processed, size_t current_write,
                                                     size_t max_samples) {
  if (last_processed == current_write) {
    return 0;
  }

  if (current_write >= last_processed) {
    return current_write - last_processed;
  } else {
    // Write index wrapped around.
    return (max_samples - last_processed) + current_write;
  }
}

double PeakEwmaLoadBalancer::calculateTimeBasedAlpha(uint64_t current_time_ns,
                                                     uint64_t sample_time_ns) {
  int64_t time_delta_ns = static_cast<int64_t>(current_time_ns - sample_time_ns);
  if (time_delta_ns <= 0) {
    return 1.0; // Use full weight for future/concurrent samples.
  }

  // Time-based exponential decay: α = 1 - e^(-Δt/τ).
  double time_delta_s = time_delta_ns / 1000000000.0;
  double tau_s = tau_nanos_ / 1000000000.0;
  double alpha = 1.0 - std::exp(-time_delta_s / tau_s);

  // Clamp alpha to reasonable bounds.
  return std::min(1.0, std::max(0.0, alpha));
}

double PeakEwmaLoadBalancer::updateEwmaWithSample(double current_ewma, double new_rtt_ms,
                                                  double alpha) {
  if (current_ewma == 0.0) {
    // First sample - initialize EWMA.
    return new_rtt_ms;
  }

  // EWMA update: new_ewma = α × new_rtt + (1-α) × old_ewma.
  return alpha * new_rtt_ms + (1.0 - alpha) * current_ewma;
}

void PeakEwmaLoadBalancer::processHostSamples(Upstream::HostConstSharedPtr /* host */,
                                              PeakEwmaHostLbPolicyData* data) {
  if (!data)
    return;

  // Get the range of new samples to process (atomic ring buffer).
  auto [last_processed, current_write] = data->getNewSampleRange();

  size_t num_new_samples =
      calculateNewSampleCount(last_processed, current_write, data->max_samples_);
  if (num_new_samples == 0)
    return;

  // Get current EWMA state.
  double current_ewma = data->getEwmaRtt();
  uint64_t current_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 time_source_.monotonicTime().time_since_epoch())
                                 .count();

  // Process all new samples in chronological order.
  size_t processed_index = last_processed;
  for (size_t i = 0; i < num_new_samples; ++i) {
    size_t ring_index = processed_index % data->max_samples_;

    double rtt_ms = data->rtt_samples_[ring_index].load();
    uint64_t timestamp_ns = data->timestamps_[ring_index].load();

    // Skip invalid samples (should be rare).
    if (rtt_ms <= 0.0 || timestamp_ns == 0) {
      processed_index++;
      continue;
    }

    double alpha = calculateTimeBasedAlpha(current_time_ns, timestamp_ns);
    current_ewma = updateEwmaWithSample(current_ewma, rtt_ms, alpha);
    processed_index++;
  }

  // Update atomic EWMA in host data.
  data->updateEwma(current_ewma, current_time_ns);
  data->markSamplesProcessed(current_write);
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
