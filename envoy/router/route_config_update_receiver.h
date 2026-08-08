#pragma once

#include <memory>
#include <optional>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/rds/route_config_update_receiver.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Router {

/**
 * The part of a route configuration receiver that a VHDS subscription drives. Split out so that the
 * subscription only depends on - and a test only has to mock - the one callback it uses.
 */
class VhdsConfigUpdateReceiver {
public:
  virtual ~VhdsConfigUpdateReceiver() = default;

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
                            std::set<std::string>&& added_resource_ids,
                            const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                            const std::string& version_info) PURE;
};

/**
 * A primitive that keeps track of updates to a RouteConfiguration.
 */
class RouteConfigUpdateReceiver : public Rds::RouteConfigUpdateReceiver {
public:
  /**
   * Same purpose as Rds::RouteConfigUpdateReceiver::protobufConfiguration()
   * but the return is downcasted to proper type.
   * @return current RouteConfiguration downcasted from Protobuf::Message&
   */
  virtual const envoy::config::route::v3::RouteConfiguration&
  protobufConfigurationCast() const PURE;

  /**
   * Requests an on-demand VHDS update for the given alias. Does nothing if the current route
   * configuration doesn't configure VHDS.
   * @param alias supplies the alias of the virtual host to fetch.
   */
  virtual void updateOnDemand(const std::string& alias) PURE;

  /**
   * @return the union of all resource names and aliases (if any) received with the last VHDS
   * update.
   */
  virtual const std::set<std::string>& resourceIdsInLastVhdsUpdate() const PURE;
};

using RouteConfigUpdatePtr = std::unique_ptr<RouteConfigUpdateReceiver>;

} // namespace Router
} // namespace Envoy
