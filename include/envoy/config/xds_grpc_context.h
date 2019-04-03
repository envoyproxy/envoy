#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * Interface to an object that uses a Config::GrpcStream to carry out the xDS protocol.
 * TODO(fredlas) implement delta ADS with this interface. Regular ADS (GrpcMux) should also fit this
interface. class XdsGrpcContext { public: virtual ~XdsGrpcContext() {} virtual void
addSubscription(const std::vector<std::string>& resources, const std::string& type_url,
                               SubscriptionCallbacks& callbacks,
                               SubscriptionStats stats) PURE;
  virtual void pauseSubscription(const std::string& type_url) PURE;
  virtual void resumeSubscription(const std::string& type_url) PURE;
  virtual void updateResources(const std::vector<std::string>& resources,
                               const std::string& type_url) PURE;
  virtual void removeSubscription(const std::string& type_url) PURE;
};
*/

/**
 * A grouping of callbacks that an XdsGrpcContext should provide to its GrpcStream.
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

} // namespace Config
} // namespace Envoy
