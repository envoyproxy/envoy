#pragma once

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

// Redeclare this here in order to get around cyclical dependencies.
typedef std::vector<uint32_t> PriorityLoad;

/**
 * Used to optionally modify the PriorityLoad when selecting a priority for
 * a retry attempt.
 *
 * Each RetryPriority will live throughout the lifetime of a request and updated
 * with attempted hosts through onHostAttempted.
 */
class RetryPriority {
public:
  virtual ~RetryPriority() {}

  /**
   * Determines what PriorityLoad to use.
   *
   * @param priority_set current priority set of cluster.
   * @param original_priority the unmodified PriorityLoad.
   * @return a reference to the PriorityLoad to use. Return original_priority if no changes should
   * be made.
   */
  virtual const PriorityLoad& determinePriorityLoad(const PrioritySet& priority_set,
                                                    const PriorityLoad& original_priority) PURE;

  /**
   * Called after a host has been attempted but before host selection for the next attempt has
   * begun.
   *
   * @param attempted_host the host that was previously attempted.
   */
  virtual void onHostAttempted(HostDescriptionConstSharedPtr attempted_host) PURE;
};

typedef std::shared_ptr<RetryPriority> RetryPrioritySharedPtr;

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
  virtual ~RetryHostPredicate() {}

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

  virtual void createRetryPriority(RetryPriorityFactoryCallbacks& callbacks,
                                   const Protobuf::Message& config, uint32_t retry_count) PURE;

  virtual std::string name() const PURE;

  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;
};

/**
 * Factory for RetryHostPredicate.
 */
class RetryHostPredicateFactory {
public:
  virtual ~RetryHostPredicateFactory() {}

  virtual void createHostPredicate(RetryHostPredicateFactoryCallbacks& callbacks,
                                   const Protobuf::Message& config, uint32_t retry_count) PURE;

  /**
   * @return name name of this factory.
   */
  virtual std::string name() PURE;

  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;
};

} // namespace Upstream
} // namespace Envoy
