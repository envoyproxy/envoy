#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class XdsGrpcContext {
public:
  virtual ~XdsGrpcContext() = default;

  /**
   * For the gRPC stream handler to prompt the context to take appropriate action in response to the
   * gRPC stream having been successfully established.
   */
  virtual void handleStreamEstablished() PURE;

  /**
   * For the gRPC stream handler to prompt the context to take appropriate action in response to
   * failure to establish the gRPC stream.
   */
  virtual void handleEstablishmentFailure() PURE;

  /**
   * For the gRPC stream handler to call when its rate limiting logic allows more requests to be
   * sent.
   */
  virtual void drainRequests() PURE;
};

} // namespace Config
} // namespace Envoy
