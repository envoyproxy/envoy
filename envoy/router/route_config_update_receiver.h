#pragma once

#include <memory>
#include <optional>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/filter.h"
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
   * Callback used to request an update to the route configuration from the management server.
   * @param for_domain supplies the domain name that virtual hosts must match on
   * @param thread_local_dispatcher thread-local dispatcher
   * @param route_config_updated_cb callback to be called when the configuration update has been
   * propagated to worker threads
   */
  virtual void requestVirtualHostsUpdate(
      const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
      std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) PURE;

  /**
   * Notifies queued VHDS callbacks that the route configuration has been published.
   */
  virtual void onVhdsConfigPublished() PURE;
};

using RouteConfigUpdatePtr = std::unique_ptr<RouteConfigUpdateReceiver>;
} // namespace Router
} // namespace Envoy
