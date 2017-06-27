#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "google/protobuf/repeated_field.h"

namespace Envoy {
namespace Config {

template <class ResourceType> class SubscriptionCallbacks {
public:
  typedef google::protobuf::RepeatedPtrField<ResourceType> ResourceVector;

  virtual ~SubscriptionCallbacks() {}

  /**
   * Called when a configuration update is received.
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @throw EnvoyException with reason if the configuration is rejected. Otherwise the configuration
   *        is accepted. Accepted configurations have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onConfigUpdate(const ResourceVector& resources) PURE;
};

/**
 * Common abstraction for subscribing to versioned config updates. This may be implemented via bidi
 * gRPC streams, periodic/long polling REST or inotify filesystem updates. ResourceType is expected
 * to be a protobuf serializable object.
 */
template <class ResourceType> class Subscription {
public:
  virtual ~Subscription() {}

  /**
   * Start a configuration subscription asynchronously. This should be called once and will continue
   * to fetch throughout the lifetime of the Subscription object.
   * @param resources vector of resource names to fetch.
   * @param callbacks the callbacks to be notified of configuration updates. The callback must not
   *        result in the deletion of the Subscription object.
   */
  virtual void start(const std::vector<std::string>& resources,
                     SubscriptionCallbacks<ResourceType>& callbacks) PURE;

  /**
   * Update the resources to fetch.
   * @param resources vector of resource names to fetch.
   */
  virtual void updateResources(const std::vector<std::string>& resources) PURE;
};

} // namespace Config
} // namespace Envoy
