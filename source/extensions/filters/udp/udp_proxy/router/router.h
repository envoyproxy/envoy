#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

/**
 * RouteEntry is an individual resolved route entry.
 */
class RouteEntry {
public:
  virtual ~RouteEntry() = default;

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;
};

using RouteEntryPtr = std::shared_ptr<RouteEntry>;

/**
 * Route holds the RouteEntry for a request.
 */
class Route {
public:
  virtual ~Route() = default;

  /**
   * @return the route entry or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeEntry() const PURE;
};

using RouteConstSharedPtr = std::shared_ptr<const Route>;
using RouteSharedPtr = std::shared_ptr<Route>;

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() = default;

  virtual RouteConstSharedPtr route(Network::Address::InstanceConstSharedPtr address) const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
