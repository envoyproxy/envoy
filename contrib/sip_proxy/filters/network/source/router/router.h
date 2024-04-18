#pragma once

#include "envoy/router/router.h"

#include "contrib/sip_proxy/filters/network/source/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

class TransactionInfo;
using TransactionInfos = std::map<std::string, std::shared_ptr<TransactionInfo>>;

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

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * Based on the incoming Sip request transport and/or protocol data, determine the target
   * route for the request.
   * @param metadata MessageMetadata for the message to route
   * @param random_value uint64_t used to select cluster affinity
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(MessageMetadata& metadata) const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

#define ALL_SIP_ROUTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                            \
  COUNTER(route_missing)                                                                           \
  COUNTER(unknown_cluster)                                                                         \
  COUNTER(upstream_rq_maintenance_mode)                                                            \
  COUNTER(no_healthy_upstream)

struct RouterStats {
  ALL_SIP_ROUTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * The router filter configuration
 */
class RouterFilterConfig {
public:
  virtual ~RouterFilterConfig() = default;

  virtual RouterStats& stats() PURE;
};

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
