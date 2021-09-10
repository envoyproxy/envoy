#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/router/rds/route_config_provider.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {
namespace Rds {

/**
 * A primitive that keeps track of updates to a RouteConfiguration.
 */
template <class RouteConfiguration, class Config> class RouteConfigUpdateReceiver {
public:
  virtual ~RouteConfigUpdateReceiver() = default;

  /**
   * Called on updates via RDS.
   * @param rc supplies the RouteConfiguration.
   * @param version_info supplies RouteConfiguration version.
   * @return bool whether RouteConfiguration has been updated.
   */
  virtual bool onRdsUpdate(const RouteConfiguration& rc, const std::string& version_info) PURE;

  /**
   * @return std::string& the name of RouteConfiguration.
   */
  virtual const std::string& routeConfigName() const PURE;

  /**
   * @return std::string& the version of RouteConfiguration.
   */
  virtual const std::string& configVersion() const PURE;

  /**
   * @return uint64_t the hash value of RouteConfiguration.
   */
  virtual uint64_t configHash() const PURE;

  /**
   * @return absl::optional<RouteConfigProvider::ConfigInfo> containing an instance of
   * RouteConfigProvider::ConfigInfo if RouteConfiguration has been updated at least once. Otherwise
   * returns an empty absl::optional<RouteConfigProvider::ConfigInfo>.
   */
  virtual absl::optional<typename RouteConfigProvider<RouteConfiguration, Config>::ConfigInfo>
  configInfo() const PURE;

  /**
   * @return envoy::config::route::v3::RouteConfiguration& current RouteConfiguration.
   */
  virtual const RouteConfiguration& protobufConfiguration() PURE;

  /**
   * @return Router::ConfigConstSharedPtr a parsed and validated copy of current RouteConfiguration.
   * @see protobufConfiguration()
   */
  virtual std::shared_ptr<const Config> parsedConfiguration() const PURE;

  /**
   * @return SystemTime the time of the last update.
   */
  virtual SystemTime lastUpdated() const PURE;
};

template <class RouteConfiguration, class Config>
using RouteConfigUpdatePtr = std::unique_ptr<RouteConfigUpdateReceiver<RouteConfiguration, Config>>;

} // namespace Rds
} // namespace Router
} // namespace Envoy
