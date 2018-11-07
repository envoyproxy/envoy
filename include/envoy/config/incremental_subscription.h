#pragma once

#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/stats/stats_macros.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

template <class ResourceType> class IncrementalSubscriptionCallbacks {
public:
  typedef Protobuf::RepeatedPtrField<ResourceType> ResourceVector;

  virtual ~IncrementalSubscriptionCallbacks() {}

  /**
   * Called when an incremental configuration update is received.
   * @param added_resources resources newly added since the previous fetch.
   * @param removed_resources names of resources that this fetch instructed to be removed.
   * @param version_info update version.
   * @throw EnvoyException with reason if the incremental config changes are rejected. Otherwise the
   *        changes are accepted. Accepted changes have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onIncrementalConfig(const ResourceVector& added_resources,
                                   const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                   const std::string& version_info) PURE;

  /**
   * Called when either the IncrementalSubscription is unable to fetch a config update or when
   * onIncrementalConfig invokes an exception.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onIncrementalConfigFailed(const EnvoyException* e) PURE;

  /**
   * Obtain the "name" of a v2 API resource in a google.protobuf.Any, e.g. the route config name for
   * a RouteConfiguration, based on the underlying resource type.
   */
  virtual std::string resourceName(const ProtobufWkt::Any& resource) PURE;
};

/**
 * Common abstraction for subscribing to incremental config updates. Due to the incremental
 * approach's stateful nature, the service can only be implemented as a gRPC bidi stream.
 * ResourceType must be a protobuf serializable object.
 */
template <class ResourceType> class IncrementalSubscription {
public:
  virtual ~IncrementalSubscription() {}

  /**
   * Start a configuration subscription asynchronously. This should be called once and will continue
   * to fetch throughout the lifetime of the IncrementalSubscription object.
   * @param resources vector of resource names to fetch.
   * @param callbacks the callbacks to be notified of configuration updates. The callback must not
   *        result in the deletion of the IncrementalSubscription object.
   */
  virtual void start(const std::vector<std::string>& resources,
                     IncrementalSubscriptionCallbacks<ResourceType>& callbacks) PURE;

  /**
   * Update the resources to fetch.
   * @param resources vector of resource names to fetch.
   */
  virtual void updateResources(const std::vector<std::string>& resources) PURE;
};

/**
 * Per subscription stats. @see stats_macros.h
 */
// clang-format off
#define ALL_INCREMENTAL_SUBSCRIPTION_STATS(COUNTER, GAUGE) \
  COUNTER(update_attempt)                                  \
  COUNTER(update_success)                                  \
  COUNTER(update_failure)                                  \
  COUNTER(update_rejected)                                 \
  GAUGE(version)
// clang-format on

/**
 * Struct definition for per subscription stats. @see stats_macros.h
 */
struct IncrementalSubscriptionStats {
  ALL_INCREMENTAL_SUBSCRIPTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Config
} // namespace Envoy
