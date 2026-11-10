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
   * Requests an on-demand VHDS update for the given alias. Does nothing if the current route
   * configuration doesn't configure VHDS.
   * @param alias supplies the alias of the virtual host to fetch.
   */
  virtual void updateOnDemand(const std::string& alias) PURE;

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

  /**
   * @return the union of all resource names and aliases (if any) received with the last VHDS
   * update.
   */
  virtual const std::set<std::string>& resourceIdsInLastVhdsUpdate() const PURE;

  /**
   * @return whether the given resource id was requested on demand from the current VHDS
   * subscription and received back in an update that has already been published. When this
   * returns true, the published route configuration is the authoritative answer for the id,
   * including a "doesn't exist" answer that the server gave with an empty resource: a requested
   * id is part of the delta subscription interest, which is re-sent when the stream reconnects,
   * so the server pushes a new update on its own if the virtual host appears, changes or goes
   * away later. Ids the server volunteered without a request are deliberately never reported as
   * answered, because no subscription guarantees further pushes for them. Returns false while an
   * update is still warming, because the published configuration may not reflect the server's
   * answer for the id yet.
   */
  virtual bool vhdsResourceIdAnswered(const std::string& resource_id) const PURE;
};

using RouteConfigUpdatePtr = std::unique_ptr<RouteConfigUpdateReceiver>;
} // namespace Router
} // namespace Envoy
