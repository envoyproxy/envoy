#pragma once

#include <memory>
#include <string>

#include "envoy/router/router.h"

#include "extensions/filters/network/dubbo_proxy/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
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

  /**
   * @return MetadataMatchCriteria* the metadata that a subset load balancer should match when
   * selecting an upstream host
   */
  virtual const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const PURE;
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

  /**
   * Based on the incoming Dubbo request transport and/or protocol data, determine the target
   * route for the request.
   * @param metadata MessageMetadata for the message to route
   * @param random_value uint64_t used to select cluster affinity
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
