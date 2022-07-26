#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

namespace Envoy {
namespace Config {

/**
 * An interface for receiving and acting upon accepted xDS configuration updates for which an ACK
 * is being sent.
 */
class ConfigUpdatedListener {
public:
  virtual ~ConfigUpdatedListener() = default;

  /**
   * Invoked when SotW xDS configuration updates have been received from a control plane, have been
   * applied on the Envoy instance, and are about to be ACK'ed.
   * @param control_plane_id Unique id for the control plane from which the resources are obtained.
   *   Typically either a cluster name or a host name.
   * @param resource_type_url The URL for the type of the resource (e.g. Secrets, Clusters, etc).
   * @param resources The resources sent by the control plane for the given type.
   */
  virtual void onConfigUpdated(const std::string& control_plane_id,
                               const std::string& resource_type_url,
                               const std::vector<DecodedResourceRef>& resources) PURE;
};

using ConfigUpdatedListenerPtr = std::unique_ptr<ConfigUpdatedListener>;
using ConfigUpdatedListenerList = std::vector<ConfigUpdatedListenerPtr>;

} // namespace Config
} // namespace Envoy
