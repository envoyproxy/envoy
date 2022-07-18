#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

namespace Envoy {
namespace Config {

// An interface for receiving and acting upon accepted xDS configuration updates for which an ACK is
// being sent.
class ConfigUpdatedListener {
public:
  virtual ~ConfigUpdatedListener() = default;

  // TODO(abeyad): add comments
  virtual void onConfigUpdated(const std::string& control_plane_id,
                               const std::string& resource_type_url,
                               const std::vector<DecodedResourceRef>& resources) PURE;
};

using ConfigUpdatedListenerPtr = std::unique_ptr<ConfigUpdatedListener>;
using ConfigUpdatedListenerList = std::vector<ConfigUpdatedListenerPtr>;

} // namespace Config
} // namespace Envoy
