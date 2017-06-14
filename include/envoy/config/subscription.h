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
   * @return bool indicating whether the new configuration is accepted. Accepted configurations have
   *         their version_info reflected in subsequent requests.
   */
  virtual bool onConfigUpdate(const ResourceVector& resources) PURE;
};

/**
 * Common abstraction for subscribing to versioned config updates. This may be implemented via bidi
 * gRPC streams, periodic/long polling REST or inotify filesystem updates. Both ResponseType and
 * ResourceType are expected to be protobuf serializable objects. ResponseType must have two fields:
 * - bytes version_info
 * - repeated ResponseType resources
 * This typing corresponds to the stylized use of protobufs in the xDS subscription response
 * objects.
 */
template <class ResponseType, class ResourceType> class Subscription {
public:
  virtual ~Subscription() {}

  /**
   * Start a configuration subscription asynchronously. This should be called once and will continue
   * to fetch throughout the lifetime of the Subscription object.
   * @param resources vector of resource names to fetch.
   * @param callbacks the callbacks to be notified of configuration updates.
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
