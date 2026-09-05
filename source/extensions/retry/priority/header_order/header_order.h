#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

// HeaderOrderRetryPriority reads an ordered list of priority indices from a configurable
// dynamic metadata namespace/key (set upstream of the router filter, e.g. by a custom ext_proc
// filter) and forces each successive *retry* attempt to target the next priority in that list,
// overriding the cluster's default priority-based load balancing.
//
// IMPORTANT: Envoy's router only consults RetryPriority::determinePriorityLoad on retries; the
// initial attempt always uses the cluster's native priority-based load balancing and never
// calls into this plugin (see Envoy's Router::Filter::determinePriorityLoad, which
// short-circuits to original_priority_load when not retrying). Consequently, order[0]
// corresponds to the *first retry*, not the initial attempt.
//
// Example: metadata value [0, 2] (with 2 backend priorities besides whatever the initial
// attempt used) means: 1st retry -> priority 0, 2nd retry -> priority 2. If a listed priority
// has no healthy hosts, this plugin searches forward in the list for the next healthy
// priority; once the list is exhausted (or was never present/parseable), it falls back to the
// cluster's original, unmodified priority load, i.e. normal Envoy priority-based load
// balancing takes back over.
//
// Unlike envoy.retry_priorities.previous_priorities, this plugin does not track failed
// priorities to exclude them: the desired order is fully dictated up-front by the metadata,
// and each call to determinePriorityLoad advances an internal retry-attempt counter so the
// next call targets the next entry.
class HeaderOrderRetryPriority : public Upstream::RetryPriority {
public:
  HeaderOrderRetryPriority(const std::string& metadata_namespace, const std::string& metadata_key,
                           uint32_t max_retries)
      : metadata_namespace_(metadata_namespace), metadata_key_(metadata_key) {
    order_.reserve(static_cast<size_t>(max_retries) + 1);
  }

  // Upstream::RetryPriority
  const Upstream::HealthyAndDegradedLoad&
  determinePriorityLoad(StreamInfo::StreamInfo* stream_info,
                        const Upstream::PrioritySet& priority_set,
                        const Upstream::HealthyAndDegradedLoad& original_priority_load,
                        const PriorityMappingFunc& priority_mapping_func) override;

  // No-op: unlike envoy.retry_priorities.previous_priorities, this plugin does not need to
  // track which hosts were attempted -- the retry-attempt counter used to index into order_ is
  // maintained internally by determinePriorityLoad itself (see header_order.cc), since
  // onHostAttempted also fires for the initial, non-retried attempt and so cannot be used
  // directly as a retry index.
  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr) override {}

private:
  // Parses order_ from the configured dynamic metadata namespace/key. Only attempted once per
  // request: the metadata is set before the router filter runs and does not change across
  // retry attempts.
  void parseOrderOnce(StreamInfo::StreamInfo* stream_info);

  // Resets and resizes per_priority_load_ to num_priorities entries, all zeroed.
  void resetLoad(uint32_t num_priorities);

  const std::string metadata_namespace_;
  const std::string metadata_key_;
  std::vector<uint32_t> order_;
  bool order_parsed_{false};
  // Number of times determinePriorityLoad has been called for this request so far, i.e. the
  // number of retry attempts, since it is never called for the initial attempt. Used as the
  // starting index into order_ on each call, then incremented.
  uint32_t retry_attempt_index_{0};
  Upstream::HealthyAndDegradedLoad per_priority_load_;
};

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
