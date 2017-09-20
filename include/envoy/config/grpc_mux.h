#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

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
};

/**
 * Handle on an muxed gRPC subscription.  The subscription is canceled on destruction.
 */
class GrpcMuxWatch {
public:
  virtual ~GrpcMuxWatch() {}
};

typedef std::unique_ptr<GrpcMuxWatch> GrpcMuxWatchPtr;

/**
 * Manage one or more gRPC subscriptions on a single stream to management server. This can be used
 * for a single xDS API, e.g. EDS, or to combined multiple xDS APIs for ADS.
 */
class GrpcMux {
public:
  virtual ~GrpcMux() {}

  /**
   * Initiate stream with management server.
   */
  virtual void start() PURE;

  /**
   * Start a configuration subscription asynchronously for some API type and resources.
   * @param type_url type URL corresponding to xDS API, e.g.
   *                 type.googleapis.com/envoy.api.v2.Cluster.
   * @param resources vector of resource names to watch for. If this is empty, then all
   *                  resources for type_url will result in callbacks.
   * @param callbacks the callbacks to be notified of configuration updates. These must be valid
   *                  until GrpcMuxWatch is destroyed.
   * @return GrpcMuxWatchPtr a handle to cancel the subscription with. E.g. when a cluster goes
   * away, its EDS updates should be cancelled by destroying the GrpcMuxWatchPtr.
   */
  virtual GrpcMuxWatchPtr subscribe(const std::string& type_url,
                                    const std::vector<std::string>& resources,
                                    GrpcMuxCallbacks& callbacks) PURE;
};

typedef std::unique_ptr<GrpcMux> GrpcMuxPtr;

} // namespace Config
} // namespace Envoy
