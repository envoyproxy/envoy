#pragma once

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

// Redeclare this here in order to get around cyclical dependencies.
typedef std::vector<uint32_t> PriorityLoad;

/**
 * This class is used to optionally modify the Prioirtyload when selecting a priority for
 * a retry attempt.
 */
class RetryPriority {
public:
  virtual ~RetryPriority() {}

  /**
   * Determines what PriorityLoad to use for a retry attempt.
   *
   * @param priority_state current state of cluster.
   * @param original_priority the unmodified PriorityLoad.
   * @param attempted_hosts an ordered list of hosts that have been previously attempted.
   * @return a reference to the PriorityLoad to use. Return original_priority if no changes should
   * be made.
   */
  virtual PriorityLoad& determinePriorityLoad(const PriorityState& priority_state,
                                              const PriorityLoad& original_priority,
                                              const HostVector& attempted_hosts) PURE;
};

typedef std::shared_ptr<RetryPriority> RetryPrioritySharedPtr;

/**
 * Used to decide whether a selected host should be rejected during retries. Host selection will be
 * reattempted until either the host predicate accepts the host or a configured max number of
 * attempts is reached.
 */
class RetryHostPredicate {
public:
  virtual ~RetryHostPredicate() {}

  /**
   * Determines whether a host should be rejected during retries.
   *
   * @param candidate_host the host to either reject or accept.
   * @param attempted_hosts an ordered list of hosts that have been previously attempted.
   * @return whether the host should be rejected and host selection reattempted.
   */
  virtual bool shouldSelectAnotherHost(const Host& candidate_host,
                                       const HostVector& attempted_hosts) PURE;
};

typedef std::shared_ptr<RetryHostPredicate> RetryHostPredicateSharedPtr;

/**
 * Callbacks given to a RetryPriorityFactory that allows adding retry filters.
 */
class RetryPriorityFactoryCallbacks {
public:
  virtual ~RetryPriorityFactoryCallbacks() {}

  /**
   * Called by the factory to add a RetryPriority.
   */
  virtual void addRetryPriority(RetryPrioritySharedPtr filter) PURE;
};

/**
 * Callbacks given to a RetryHostPredicateFactory that allows adding retry filters.
 */
class RetryHostPredicateFactoryCallbacks {
public:
  virtual ~RetryHostPredicateFactoryCallbacks() {}

  /**
   * Called by the factory to add a RetryHostPredicate.
   */
  virtual void addHostPredicate(RetryHostPredicateSharedPtr filter) PURE;
};

/**
 * Factory for RetryPriority.
 */
class RetryPriorityFactory {
public:
  virtual ~RetryPriorityFactory() {}

  virtual void createRetryPriority(RetryPriorityFactoryCallbacks& callbacks) PURE;
};

/**
 * Factory for RetryHostPredicate.
 */
class RetryHostPredicateFactory {
public:
  virtual ~RetryHostPredicateFactory() {}

  virtual void createHostPredicate(RetryHostPredicateFactoryCallbacks& callbacks) PURE;
};

} // namespace Upstream
} // namespace Envoy
