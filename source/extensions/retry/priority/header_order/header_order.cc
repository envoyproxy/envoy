#include "source/extensions/retry/priority/header_order/header_order.h"

#include "source/common/config/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

void HeaderOrderRetryPriority::parseOrderOnce(StreamInfo::StreamInfo* stream_info) {
  if (stream_info == nullptr) {
    return;
  }
  const Protobuf::Value& value = Envoy::Config::Metadata::metadataValue(
      &stream_info->dynamicMetadata(), metadata_namespace_, metadata_key_);
  if (value.kind_case() != Protobuf::Value::kListValue) {
    return;
  }
  for (const Protobuf::Value& entry : value.list_value().values()) {
    if (entry.kind_case() != Protobuf::Value::kNumberValue) {
      continue;
    }
    const double num = entry.number_value();
    if (num >= 0) {
      order_.push_back(static_cast<uint32_t>(num));
    }
  }
}

void HeaderOrderRetryPriority::resetLoad(uint32_t num_priorities) {
  if (per_priority_load_.healthy_priority_load_.get().size() != num_priorities) {
    per_priority_load_.healthy_priority_load_.get().assign(num_priorities, 0);
    per_priority_load_.degraded_priority_load_.get().assign(num_priorities, 0);
    return;
  }
  std::fill(per_priority_load_.healthy_priority_load_.get().begin(),
            per_priority_load_.healthy_priority_load_.get().end(), 0);
  std::fill(per_priority_load_.degraded_priority_load_.get().begin(),
            per_priority_load_.degraded_priority_load_.get().end(), 0);
}

const Upstream::HealthyAndDegradedLoad& HeaderOrderRetryPriority::determinePriorityLoad(
    StreamInfo::StreamInfo* stream_info, const Upstream::PrioritySet& priority_set,
    const Upstream::HealthyAndDegradedLoad& original_priority_load,
    const PriorityMappingFunc&) {
  if (!order_parsed_) {
    parseOrderOnce(stream_info);
    order_parsed_ = true;
  }

  const uint32_t num_priorities = static_cast<uint32_t>(priority_set.hostSetsPerPriority().size());

  // Each call to this method corresponds to exactly one retry attempt (never the initial
  // attempt -- see the class comment), so retry_attempt_index_ before incrementing is this
  // retry's 0-based position: 0 for the first retry, 1 for the second, and so on.
  const uint32_t start_idx = retry_attempt_index_;
  ++retry_attempt_index_;

  // Starting from this retry's position in the desired order, pick the first entry that maps
  // to a priority with at least one healthy host. This naturally "skips ahead" past priorities
  // that are configured in the metadata but currently unhealthy, instead of getting stuck
  // retrying a dead priority.
  for (uint32_t idx = start_idx; idx < order_.size(); ++idx) {
    const uint32_t priority = order_[idx];
    if (priority >= num_priorities) {
      continue;
    }
    const auto& host_set = *priority_set.hostSetsPerPriority()[priority];
    if (!host_set.healthyHosts().empty()) {
      resetLoad(num_priorities);
      per_priority_load_.healthy_priority_load_.get()[priority] = 100;
      return per_priority_load_;
    }
  }

  // No (more) usable entries in the metadata-supplied order: fall back to the cluster's
  // default priority-based load balancing for this attempt.
  return original_priority_load;
}

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
