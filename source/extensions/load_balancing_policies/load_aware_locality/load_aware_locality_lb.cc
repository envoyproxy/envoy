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

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
