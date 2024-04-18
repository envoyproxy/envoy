#pragma once

#include <memory>

#include "envoy/common/time.h"
#include "envoy/rds/config.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Rds {

/**
 * A provider for constant route configurations.
 */
class RouteConfigProvider {
public:
  struct ConfigInfo {
    // A reference to the currently loaded route configuration. Do not hold this reference beyond
    // the caller of configInfo()'s scope.
    const Protobuf::Message& config_;

    // The discovery version that supplied this route. This will be set to "" in the case of
    // static clusters.
    const std::string version_;
  };

  virtual ~RouteConfigProvider() = default;

  /**
   * @return ConfigConstSharedPtr a route configuration for use during a single request. The
   * returned config may be different on a subsequent call, so a new config should be acquired for
   * each request flow.
   */
  virtual ConfigConstSharedPtr config() const PURE;

  /**
   * @return the configuration information for the currently loaded route configuration. Note that
   * if the provider has not yet performed an initial configuration load, no information will be
   * returned.
   */
  virtual const absl::optional<ConfigInfo>& configInfo() const PURE;

  /**
   * @return the last time this RouteConfigProvider was updated. Used for config dumps.
   */
  virtual SystemTime lastUpdated() const PURE;

  /**
   * Callback used to notify RouteConfigProvider about configuration changes.
   * @return Status indicating if the call was successful or had graceful error handling.
   */
  virtual absl::Status onConfigUpdate() PURE;
};

using RouteConfigProviderPtr = std::unique_ptr<RouteConfigProvider>;
using RouteConfigProviderSharedPtr = std::shared_ptr<RouteConfigProvider>;

} // namespace Rds
} // namespace Envoy
