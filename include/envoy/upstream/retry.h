#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/upstream/types.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * Used to optionally modify the PriorityLoad when selecting a priority for
 * a retry attempt.
 *
 * Each RetryPriority will live throughout the lifetime of a request and updated
 * with attempted hosts through onHostAttempted.
 */
class RetryPriority {
public:
  virtual ~RetryPriority() = default;

  /**
   * Function that maps a HostDescription to it's effective priority level in a cluster.
   * For most cluster types, the mapping is simply `return host.priority()`, but some
   * cluster types require more complex mapping.
   * @return either the effective priority, or absl::nullopt if the mapping cannot be determined,
   *         which can happen if the host has been removed from the configurations since it was
   *         used.
   */
  using PriorityMappingFunc =
      std::function<absl::optional<uint32_t>(const Upstream::HostDescription&)>;

  static absl::optional<uint32_t> defaultPriorityMapping(const Upstream::HostDescription& host) {
    return host.priority();
  }

  /**
   * Determines what PriorityLoad to use.
   *
   * @param priority_set current priority set of cluster.
   * @param original_priority_load the unmodified HealthAndDegradedLoad.
   * @param priority_mapping_func a callback to get the priority of a host that has
   *        been attempted. This function may only be called on hosts that were
   *        passed to calls to `onHostAttempted()` on this object.
   * @return HealthAndDegradedLoad load that should be used for the next retry. Return
   * original_priority_load if the original load should be used. a pointer to original_priority,
   * original_degraded_priority if no changes should be made.
   */
  virtual const HealthyAndDegradedLoad&
  determinePriorityLoad(const PrioritySet& priority_set,
                        const HealthyAndDegradedLoad& original_priority_load,
                        const PriorityMappingFunc& priority_mapping_func) PURE;

  /**
   * Called after a host has been attempted but before host selection for the next attempt has
   * begun.
   *
   * @param attempted_host the host that was previously attempted.
   */
  virtual void onHostAttempted(HostDescriptionConstSharedPtr attempted_host) PURE;
};

using RetryPrioritySharedPtr = std::shared_ptr<RetryPriority>;

/**
 * Used to decide whether a selected host should be rejected during retries. Host selection will be
 * reattempted until either the host predicate accepts the host or a configured max number of
 * attempts is reached.
 *
 * Each RetryHostPredicate will live throughout the lifetime of a request and updated
 * with attempted hosts through onHostAttempted.
 */
class RetryHostPredicate {
public:
  virtual ~RetryHostPredicate() = default;

  /**
   * Determines whether a host should be rejected during host selection.
   *
   * @param candidate_host the host to either reject or accept.
   * @return whether the host should be rejected and host selection reattempted.
   */
  virtual bool shouldSelectAnotherHost(const Host& candidate_host) PURE;

  /**
   * Called after a host has been attempted but before host selection for the next attempt has
   * begun.
   *
   * @param attempted_host the host that was previously attempted.
   */
  virtual void onHostAttempted(HostDescriptionConstSharedPtr attempted_host) PURE;
};

using RetryHostPredicateSharedPtr = std::shared_ptr<RetryHostPredicate>;

/**
 * Factory for RetryPriority.
 */
class RetryPriorityFactory : public Config::TypedFactory {
public:
  ~RetryPriorityFactory() override = default;

  virtual RetryPrioritySharedPtr
  createRetryPriority(const Protobuf::Message& config,
                      ProtobufMessage::ValidationVisitor& validation_visitor,
                      uint32_t retry_count) PURE;

  std::string category() const override { return "envoy.retry_priorities"; }
};

/**
 * Factory for RetryHostPredicate.
 */
class RetryHostPredicateFactory : public Config::TypedFactory {
public:
  ~RetryHostPredicateFactory() override = default;

  virtual RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message& config,
                                                          uint32_t retry_count) PURE;

  std::string category() const override { return "envoy.retry_host_predicates"; }
};

} // namespace Upstream
} // namespace Envoy
