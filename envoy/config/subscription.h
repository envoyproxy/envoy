#pragma once

#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/protobuf/protobuf.h"

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

/**
 * A wrapper for xDS resources that have been deserialized from the wire.
 */
class DecodedResource {
public:
  virtual ~DecodedResource() = default;

  /**
   * @return const std::string& resource name.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return const std::vector<std::string& resource aliases.
   */
  virtual const std::vector<std::string>& aliases() const PURE;

  /**
   * @return const std::string& resource version.
   */
  virtual const std::string& version() const PURE;

  /**
   * @return const Protobuf::Message& resource message reference. If hasResource() is false, this
   *         will be the empty message.
   */
  virtual const Protobuf::Message& resource() const PURE;

  virtual absl::optional<std::chrono::milliseconds> ttl() const PURE;

  /**
   * @return bool does the xDS discovery response have a set resource payload?
   */
  virtual bool hasResource() const PURE;

  /**
   * @return optional ref<envoy::config::core::v3::Metadata> of a resource.
   */
  virtual const OptRef<const envoy::config::core::v3::Metadata> metadata() const PURE;
};

using DecodedResourcePtr = std::unique_ptr<DecodedResource>;
using DecodedResourceRef = std::reference_wrapper<DecodedResource>;

class OpaqueResourceDecoder {
public:
  virtual ~OpaqueResourceDecoder() = default;

  /**
   * @param resource some opaque resource (ProtobufWkt::Any).
   * @return ProtobufTypes::MessagePtr decoded protobuf message in the opaque resource, e.g. the
   *         RouteConfiguration for an Any containing envoy.config.route.v3.RouteConfiguration.
   */
  virtual ProtobufTypes::MessagePtr decodeResource(const ProtobufWkt::Any& resource) PURE;

  /**
   * @param resource some opaque resource (Protobuf::Message).
   * @return std::String the resource name in a Protobuf::Message returned by decodeResource(), e.g.
   *         the route config name for a envoy.config.route.v3.RouteConfiguration message.
   */
  virtual std::string resourceName(const Protobuf::Message& resource) PURE;
};

using OpaqueResourceDecoderSharedPtr = std::shared_ptr<OpaqueResourceDecoder>;

/**
 * Subscription to DecodedResources.
 */
class SubscriptionCallbacks {
public:
  virtual ~SubscriptionCallbacks() = default;

  /**
   * Called when a state-of-the-world configuration update is received. (State-of-the-world is
   * everything other than delta gRPC - filesystem, HTTP, non-delta gRPC).
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @param version_info supplies the version information as supplied by the xDS discovery response.
   * @return an absl status indicating if a non-exception-throwing error was encountered.
   * @throw EnvoyException with reason if the configuration is rejected for legacy reasons,
   *        Accepted configurations have their version_info reflected in subsequent requests.
   */
  virtual absl::Status onConfigUpdate(const std::vector<DecodedResourceRef>& resources,
                                      const std::string& version_info) PURE;

  /**
   * Called when a delta configuration update is received.
   * @param added_resources resources newly added since the previous fetch.
   * @param removed_resources names of resources that this fetch instructed to be removed.
   * @param system_version_info aggregate response data "version", for debugging.
   * @return an absl status indicating if a non-exception-throwing error was encountered.
   * @throw EnvoyException with reason if the configuration is rejected for legacy reasons,
   *        Accepted configurations have their version_info reflected in subsequent requests.
   */
  virtual absl::Status
  onConfigUpdate(const std::vector<DecodedResourceRef>& added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info) PURE;

  /**
   * Called when either the Subscription is unable to fetch a config update or when onConfigUpdate
   * returns a failure or invokes an exception.
   * @param reason supplies the update failure reason.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) PURE;
};

/**
 * Options associated with a Subscription.
 */
struct SubscriptionOptions {
  /**
   * For legacy VHDS, should an xDS resource name be treated as <namespace>/<resource name>? This is
   * incompatible with the use of xdstp:// naming.
   */
  bool use_namespace_matching_{};

  /**
   * For xdstp:// resource names, should node context parameters be added at the transport layer?
   */
  bool add_xdstp_node_context_params_{};
};

/**
 * Invoked when raw config received from xDS wire.
 */
class UntypedConfigUpdateCallbacks {
public:
  virtual ~UntypedConfigUpdateCallbacks() = default;

  // TODO (dmitri-d) remove this method when legacy sotw mux has been removed.
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
   * Called when a non-delta gRPC configuration update is received.
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @param version_info supplies the version information as supplied by the xDS discovery response.
   * @throw EnvoyException with reason if the configuration is rejected. Otherwise the configuration
   *        is accepted. Accepted configurations have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onConfigUpdate(const std::vector<DecodedResourcePtr>& resources,
                              const std::string& version_info) PURE;

  /**
   * Called when a delta configuration update is received.
   * @param added_resources resources newly added since the previous fetch.
   * @param removed_resources names of resources that this fetch instructed to be removed.
   * @param system_version_info aggregate response data "version", for debugging.
   * @throw EnvoyException with reason if the config changes are rejected. Otherwise the changes
   * @param use_namespace_matching if the resources should me matched on their namespaces, rather
   * than unique names. This is used when a collection of resources (e.g. virtual hosts in VHDS) is
   * being updated. Accepted changes have their version_info reflected in subsequent
   * requests.
   */
  virtual void
  onConfigUpdate(absl::Span<const envoy::service::discovery::v3::Resource* const> added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info) PURE;

  /**
   * Called when either the Subscription is unable to fetch a config update or when onConfigUpdate
   * invokes an exception.
   * @param reason supplies the update failure reason.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) PURE;
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
  virtual void start(const absl::flat_hash_set<std::string>& resource_names) PURE;

  /**
   * Update the resources to fetch.
   * @param resources vector of resource names to fetch.
   */
  virtual void
  updateResourceInterest(const absl::flat_hash_set<std::string>& update_to_these_names) PURE;

  /**
   * Creates a discovery request for resources.
   * @param add_these_names resource ids for inclusion in the discovery request.
   */
  virtual void requestOnDemandUpdate(const absl::flat_hash_set<std::string>& add_these_names) PURE;
};

using SubscriptionPtr = std::unique_ptr<Subscription>;

/**
 * Per subscription stats. @see stats_macros.h
 */
#define ALL_SUBSCRIPTION_STATS(COUNTER, GAUGE, TEXT_READOUT, HISTOGRAM)                            \
  COUNTER(init_fetch_timeout)                                                                      \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_failure)                                                                          \
  COUNTER(update_rejected)                                                                         \
  COUNTER(update_success)                                                                          \
  GAUGE(update_time, NeverImport)                                                                  \
  GAUGE(version, NeverImport)                                                                      \
  HISTOGRAM(update_duration, Milliseconds)                                                         \
  TEXT_READOUT(version_text)

/**
 * Struct definition for per subscription stats. @see stats_macros.h
 */
struct SubscriptionStats {
  ALL_SUBSCRIPTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                         GENERATE_TEXT_READOUT_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

} // namespace Config
} // namespace Envoy
