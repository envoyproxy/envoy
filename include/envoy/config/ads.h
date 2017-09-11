#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class AdsCallbacks {
public:
  virtual ~AdsCallbacks() {}

  /**
   * Called when a configuration update is received.
   * @param resources vector of fetched resources corresponding to the configuration update.
   * @throw EnvoyException with reason if the configuration is rejected. Otherwise the configuration
   *        is accepted. Accepted configurations have their version_info reflected in subsequent
   *        requests.
   */
  virtual void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources) PURE;

  /**
   * Called when either the subscription is unable to fetch a config update or when onConfigUpdate
   * invokes an exception.
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onConfigUpdateFailed(const EnvoyException* e) PURE;
};

/**
 * Handle on an ADS subscription.  The subscription is canceled on destruction.
 */
class AdsWatch {
public:
  virtual ~AdsWatch() {}
};

typedef std::unique_ptr<AdsWatch> AdsWatchPtr;

/**
 * Aggregated Discovery Service interface.
 */
class AdsApi {
public:
  virtual ~AdsApi() {}

  /**
   * Start a configuration subscription asynchronously for some API type and resources.
   * @param type_url type URL corresponding to xDS API, e.g.
   *                 type.googleapis.com/envoy.api.v2.Cluster.
   * @param resources vector of resource names to fetch.
   * @param callbacks the callbacks to be notified of configuration updates. These must be valid
   *                  until AdsWatch::cancel() is invoked.
   * @return AdsWatchPtr a handle to cancel the subscription with. E.g. when a cluster goes away,
   * its EDS updates should be cancelled. Ownership of the AdsWatch belongs to the AdsApi object.
   */
  virtual AdsWatchPtr subscribe(const std::string& type_url,
                                const std::vector<std::string>& resources,
                                AdsCallbacks& calllbacks) PURE;
};

} // namespace Config
} // namespace Envoy
