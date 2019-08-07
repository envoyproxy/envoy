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
#define ALL_CONTROL_PLANE_STATS(COUNTER, GAUGE)                                                    \
  COUNTER(rate_limit_enforced)                                                                     \
  GAUGE(connected_state, NeverImport)                                                              \
  GAUGE(pending_requests, Accumulate)

/**
 * Struct definition for all control plane stats. @see stats_macros.h
 */
struct ControlPlaneStats {
  ALL_CONTROL_PLANE_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

// TODO(fredlas) redundant to SubscriptionCallbacks; remove this one.
class GrpcMuxCallbacks {
public:
  virtual ~GrpcMuxCallbacks() = default;

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
   * @param reason supplies the update failure reason.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                    const EnvoyException* e) PURE;

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
  virtual ~GrpcMuxWatch() = default;
};

using GrpcMuxWatchPtr = std::unique_ptr<GrpcMuxWatch>;

/**
 * Manage one or more gRPC subscriptions on a single stream to management server. This can be used
 * for a single xDS API, e.g. EDS, or to combined multiple xDS APIs for ADS.
 */
class GrpcMux {
public:
  virtual ~GrpcMux() = default;

  /**
   * Initiate stream with management server.
   */
  virtual void start() PURE;

  /**
   * Start a configuration subscription asynchronously for some API type and resources.
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   * @param resources set of resource names to watch for. If this is empty, then all
   *                  resources for type_url will result in callbacks.
   * @param callbacks the callbacks to be notified of configuration updates. These must be valid
   *                  until GrpcMuxWatch is destroyed.
   * @return GrpcMuxWatchPtr a handle to cancel the subscription with. E.g. when a cluster goes
   * away, its EDS updates should be cancelled by destroying the GrpcMuxWatchPtr.
   */
  virtual GrpcMuxWatchPtr subscribe(const std::string& type_url,
                                    const std::set<std::string>& resources,
                                    GrpcMuxCallbacks& callbacks) PURE;

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
   * @param type_url type URL corresponding to xDS API e.g. type.googleapis.com/envoy.api.v2.Cluster
   */
  virtual void resume(const std::string& type_url) PURE;

  /**
   * Retrieves the current pause state as set by pause()/resume().
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster
   * @return bool whether the API is paused.
   */
  virtual bool paused(const std::string& type_url) const PURE;
};

using GrpcMuxPtr = std::unique_ptr<GrpcMux>;

} // namespace Config
} // namespace Envoy
