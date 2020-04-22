#pragma once

#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/stats_macros.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * Reason that a config update is failed.
 */
enum class ConfigUpdateFailureReason {
  // A connection failure took place and the update could not be fetched.
  ConnectionFailure,
  // Config fetch timed out.
  FetchTimedout,
  // Update rejected because there is a problem in applying the update.
  UpdateRejected
};

class SubscriptionCallbacks {
public:
  virtual ~SubscriptionCallbacks() = default;

  /**
   * Called when a state-of-the-world configuration update is received. (State-of-the-world is
   * everything other than delta gRPC - filesystem, HTTP, non-delta gRPC).
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @param version_info supplies the version information as supplied by the xDS discovery response.
   * @throw EnvoyException with reason if the configuration is rejected. Otherwise the configuration
   *        is accepted. Accepted configurations have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) PURE;

  /**
   * Called when a delta configuration update is received.
   * @param added_resources resources newly added since the previous fetch.
   * @param removed_resources names of resources that this fetch instructed to be removed.
   * @param system_version_info aggregate response data "version", for debugging.
   * @throw EnvoyException with reason if the config changes are rejected. Otherwise the changes
   *        are accepted. Accepted changes have their version_info reflected in subsequent requests.
   */
  virtual void onConfigUpdate(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& system_version_info) PURE;

  /**
   * Called when either the Subscription is unable to fetch a config update or when onConfigUpdate
   * invokes an exception.
   * @param reason supplies the update failure reason.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) PURE;

  /**
   * Obtain the "name" of a v2 API resource in a google.protobuf.Any, e.g. the route config name for
   * a RouteConfiguration, based on the underlying resource type.
   */
  virtual std::string resourceName(const ProtobufWkt::Any& resource) PURE;
};

/**
 * Common abstraction for subscribing to versioned config updates. This may be implemented via bidi
 * gRPC streams, periodic/long polling REST or inotify filesystem updates.
 */
class Subscription {
public:
  virtual ~Subscription() = default;

  /**
   * Start a configuration subscription asynchronously. This should be called once and will continue
   * to fetch throughout the lifetime of the Subscription object.
   * @param resources set of resource names to fetch.
   */
  virtual void start(const std::set<std::string>& resource_names) PURE;

  /**
   * Update the resources to fetch.
   * @param resources vector of resource names to fetch. It's a (not unordered_)set so that it can
   * be passed to std::set_difference, which must be given sorted collections.
   */
  virtual void updateResourceInterest(const std::set<std::string>& update_to_these_names) PURE;
};

using SubscriptionPtr = std::unique_ptr<Subscription>;

/**
 * Per subscription stats. @see stats_macros.h
 */
#define ALL_SUBSCRIPTION_STATS(COUNTER, GAUGE)                                                     \
  COUNTER(init_fetch_timeout)                                                                      \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_failure)                                                                          \
  COUNTER(update_rejected)                                                                         \
  COUNTER(update_success)                                                                          \
  GAUGE(update_time, NeverImport)                                                                  \
  GAUGE(version, NeverImport)

/**
 * Struct definition for per subscription stats. @see stats_macros.h
 */
struct SubscriptionStats {
  ALL_SUBSCRIPTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Config
} // namespace Envoy
