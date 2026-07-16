#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>

#include "envoy/stats/stats_macros.h"

#include "source/common/protobuf/utility.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"
#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

absl::Status LocalityLbHostData::onOrcaLoadReport(const Upstream::OrcaLoadReport& report,
                                                  const StreamInfo::StreamInfo&) {
  const double util =
      Common::OrcaLoadReportHandler::getUtilizationFromOrcaReport(report, *metric_names_);
  // Signal-less reports do not refresh freshness; they age out instead of pinning idle.
  if (util <= 0.0) {
    return absl::OkStatus();
  }
  storeUtilization(util, time_source_.monotonicTime());
  return absl::OkStatus();
}

// --- WorkerLocalLbFactory ---

WorkerLocalLbFactory::WorkerLocalLbFactory(
    Upstream::LoadBalancerFactorySharedPtr child_worker_factory,
    LoadBalancerConfigSharedPtr child_config, const Upstream::ClusterInfo& cluster_info,
    Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random,
    ThreadLocal::SlotAllocator& tls_slot_allocator)
    : child_worker_factory_(std::move(child_worker_factory)),
      child_config_(std::move(child_config)), cluster_info_(cluster_info), random_(random),
      runtime_(runtime), healthy_panic_threshold_(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
                             cluster_info.lbConfig(), healthy_panic_threshold, 100, 50)) {
  tls_ = ThreadLocal::TypedSlot<ThreadLocalShim>::makeUnique(tls_slot_allocator);
  tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalShim>(); });
}

Upstream::LoadBalancerPtr
WorkerLocalLbFactory::createWorkerChildLb(Upstream::PrioritySetImpl& per_locality_priority_set) {
  // Non-null is guaranteed: the main-thread LB fails initialize() when the child policy could not
  // be instantiated, so the cluster never reaches workers.
  ASSERT(child_worker_factory_ != nullptr);
  Upstream::LoadBalancerParams child_params{per_locality_priority_set, nullptr};
  return child_worker_factory_->create(child_params);
}

bool WorkerLocalLbFactory::recreateChildOnHostChange() const {
  ASSERT(child_worker_factory_ != nullptr);
  return child_worker_factory_->recreateOnHostChangeDeprecated();
}

Upstream::LoadBalancerPtr WorkerLocalLbFactory::create(Upstream::LoadBalancerParams params) {
  return std::make_unique<WorkerLocalLb>(*this, params.priority_set);
}

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
