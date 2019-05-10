#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/stats_macros.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * All control plane related stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CONTROL_PLANE_STATS(COUNTER, GAUGE)                                                    \
  COUNTER(rate_limit_enforced)                                                                     \
  GAUGE(connected_state)                                                                           \
  GAUGE(pending_requests)                                                                          \
// clang-format on

/**
 * Struct definition for all control plane stats. @see stats_macros.h
 */
struct ControlPlaneStats {
  ALL_CONTROL_PLANE_STATS(GENERATE_COUNTER_STRUCT,GENERATE_GAUGE_STRUCT)
};

// TODO TODO remove. remove this whole file.
class GrpcMuxCallbacks {
public:
  virtual ~GrpcMuxCallbacks() {}

  /**
   * Called when a configuration update is received.
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @param version_info update version.
   * @throw EnvoyException with reason if the configuration is rejected. Otherwise the configuration
   *        is accepted. Accepted configurations have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) PURE;

  /**
   * Called when either the subscription is unable to fetch a config update or when onConfigUpdate
   * invokes an exception.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(const EnvoyException* e) PURE;

  /**
   * Obtain the "name" of a v2 API resource in a google.protobuf.Any, e.g. the route config name for
   * a RouteConfiguration, based on the underlying resource type.
   */
  virtual std::string resourceName(const ProtobufWkt::Any& resource) PURE;
};

/**
 * Handle on an muxed gRPC subscription. The subscription is canceled on destruction.
 */
class GrpcMuxWatch {
public:
  virtual ~GrpcMuxWatch() {}
};

typedef std::unique_ptr<GrpcMuxWatch> GrpcMuxWatchPtr;

/**
 * A grouping of callbacks that a GrpcMux should provide to its GrpcStream.
 */
template <class ResponseProto> class GrpcStreamCallbacks {
public:
  virtual ~GrpcStreamCallbacks() {}

  /**
   * For the GrpcStream to prompt the context to take appropriate action in response to the
   * gRPC stream having been successfully established.
   */
  virtual void onStreamEstablished() PURE;

  /**
   * For the GrpcStream to prompt the context to take appropriate action in response to
   * failure to establish the gRPC stream.
   */
  virtual void onEstablishmentFailure() PURE;

  /**
   * For the GrpcStream to pass received protos to the context.
   */
  virtual void onDiscoveryResponse(std::unique_ptr<ResponseProto>&& message) PURE;

  /**
   * For the GrpcStream to call when its rate limiting logic allows more requests to be sent.
   */
  virtual void onWriteable() PURE;
};

/**
 * Manage one or more gRPC subscriptions on a single stream to management server. This can be used
 * for a single xDS API, e.g. EDS, or to combined multiple xDS APIs for ADS. Both delta and state-of-the-world implement this same interface - whether a GrpcSubscriptionImpl is delta or SotW is determined entirely by which type of GrpcMux it works with.
 */
class GrpcMux {
public:
  virtual ~GrpcMux() {}
  /**
   * Starts a configuration subscription asynchronously for some API type and resources. If the gRPC
   * stream to the management server is not already up, starts it.
   * @param resources vector of resource names to watch for. If this is empty, then all
   *                  resources for type_url will result in callbacks.
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   * @param callbacks the callbacks to be notified of configuration updates. These must be valid
   *                  until this GrpcMux is destroyed.
   * @param stats reference to a stats object, which should be owned by the SubscriptionImpl, for
   * the GrpcMux to record stats specific to this one subscription.
   * @param init_fetch_timeout how long the first fetch has to complete before onConfigUpdateFailed
   * will be called.
   */
  virtual void addSubscription(const std::set<std::string>& resources, const std::string& type_url,
                               SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
                               std::chrono::milliseconds init_fetch_timeout) PURE;

  // (Un)subscribes to resources missing from / added to the passed 'resources' argument, relative
  // to the resource subscription interest currently known for type_url.
  // Attempts to send a discovery request if there is any such change.
  virtual void updateResources(const std::set<std::string>& resources,
                               const std::string& type_url) PURE;

  // Ends the given subscription, and drops the relevant parts of the protocol state.
  virtual void removeSubscription(const std::string& type_url) PURE;

  /**
   * Pause discovery requests for a given API type. This is useful when we're processing an update
   * for LDS or CDS and don't want a flood of updates for RDS or EDS respectively. Discovery
   * requests may later be resumed with resume().
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   */
  virtual void pause(const std::string& type_url) PURE;
  
  /**
   * Resume discovery requests for a given API type. This will send a discovery request if one would
   * have been sent during the pause.
   * @param type_url type URL corresponding to xDS API,
   * e.g.type.googleapis.com/envoy.api.v2.Cluster.
   */
  virtual void resume(const std::string& type_url) PURE;

  // TODO(fredlas) remove, only here for compatibility with old-style GrpcMuxImpl.
  virtual void start() PURE;
  virtual GrpcMuxWatchPtr subscribe(const std::string& type_url,
                                    const std::set<std::string>& resources,
                                    GrpcMuxCallbacks& callbacks) PURE;
};

} // namespace Config
} // namespace Envoy
