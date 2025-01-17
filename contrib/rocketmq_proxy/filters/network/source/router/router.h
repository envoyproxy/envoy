#pragma once

#include "envoy/tcp/conn_pool.h"

#include "source/common/upstream/load_balancer_context_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class ActiveMessage;
class MessageMetadata;

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

  virtual RouteConstSharedPtr route(const MessageMetadata& metadata) const PURE;
};

class Router : public Tcp::ConnectionPool::UpstreamCallbacks,
               public Upstream::LoadBalancerContextBase {

public:
  virtual void sendRequestToUpstream(ActiveMessage& active_message) PURE;

  /**
   * Release resources associated with this router.
   */
  virtual void reset() PURE;

  /**
   * Return host description that is eventually connected.
   * @return upstream host if a connection has been established; nullptr otherwise.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() PURE;
};

using RouterPtr = std::unique_ptr<Router>;
} // namespace Router
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
