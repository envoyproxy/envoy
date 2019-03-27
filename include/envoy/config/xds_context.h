#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class XdsContext {
public:
  virtual ~XdsContext() = default;

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

  // TODO(fredlas) virtual void addSubscription(...
  // TODO(fredlas) virtual void updateSubscription(...
  // TODO(fredlas) virtual void removeSubscription(const std::string& type_url) PURE;
  // TODO(fredlas) virtual void pause(const std::string& type_url) PURE;
  // TODO(fredlas) virtual void resume(const std::string& type_url) PURE;
  virtual void drainRequests() PURE;
};

} // namespace Config
} // namespace Envoy
