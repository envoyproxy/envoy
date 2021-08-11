#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/router/rds.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

/**
 * A primitive that keeps track of updates to a RouteConfiguration.
 */
class RouteConfigUpdateReceiver {
public:
  virtual ~RouteConfigUpdateReceiver() = default;

  /**
   * Called on updates via RDS.
   * @param rc supplies the RouteConfiguration.
   * @param version_info supplies RouteConfiguration version.
   * @return bool whether RouteConfiguration has been updated.
   */
  virtual bool onRdsUpdate(const envoy::config::route::v3::RouteConfiguration& rc,
                           const std::string& version_info) PURE;

  using VirtualHostRefVector =
      std::vector<std::reference_wrapper<const envoy::config::route::v3::VirtualHost>>;

  /**
   * Called on updates via VHDS.
   * @param added_vhosts supplies VirtualHosts that have been added.
   * @param added_resource_ids set of resources IDs (names + aliases) added.
   * @param removed_resources supplies names of VirtualHosts that have been removed.
   * @param version_info supplies RouteConfiguration version.
   * @return bool whether RouteConfiguration has been updated.
   */
  virtual bool onVhdsUpdate(const VirtualHostRefVector& added_vhosts,
                            const std::set<std::string>& added_resource_ids,
                            const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                            const std::string& version_info) PURE;

  /**
   * @return std::string& the name of RouteConfiguration.
   */
  virtual const std::string& routeConfigName() const PURE;

  /**
   * @return std::string& the version of RouteConfiguration.
   */
  virtual const std::string& configVersion() const PURE;

  /**
   * @return bool return whether VHDS configuration has been changed in the last RDS update.
   */
  // TODO(dmitri-d): Consider splitting RouteConfigUpdateReceiver into a RouteConfig state and a
  // last update state. The latter could be passed to callbacks as a parameter, which would make the
  // intent and the lifecycle of the "last update state" less muddled.
  virtual bool vhdsConfigurationChanged() const PURE;

  /**
   * @return uint64_t the hash value of RouteConfiguration.
   */
  virtual uint64_t configHash() const PURE;

  /**
   * @return absl::optional<RouteConfigProvider::ConfigInfo> containing an instance of
   * RouteConfigProvider::ConfigInfo if RouteConfiguration has been updated at least once. Otherwise
   * returns an empty absl::optional<RouteConfigProvider::ConfigInfo>.
   */
  virtual absl::optional<RouteConfigProvider::ConfigInfo> configInfo() const PURE;

  /**
   * @return envoy::config::route::v3::RouteConfiguration& current RouteConfiguration.
   */
  virtual const envoy::config::route::v3::RouteConfiguration& protobufConfiguration() PURE;

  /**
   * @return Router::ConfigConstSharedPtr a parsed and validated copy of current RouteConfiguration.
   * @see protobufConfiguration()
   */
  virtual ConfigConstSharedPtr parsedConfiguration() const PURE;

  /**
   * @return SystemTime the time of the last update.
   */
  virtual SystemTime lastUpdated() const PURE;

  /**
   * @return the union of all resource names and aliases (if any) received with the last VHDS
   * update.
   */
  virtual const std::set<std::string>& resourceIdsInLastVhdsUpdate() PURE;
};

using RouteConfigUpdatePtr = std::unique_ptr<RouteConfigUpdateReceiver>;
} // namespace Router
} // namespace Envoy
