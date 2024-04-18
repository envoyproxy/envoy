#pragma once

#include "envoy/common/pure.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

struct StatNames;
struct VirtualClusterStatNames;
struct RouteStatNames;

class Context {
public:
  virtual ~Context() = default;

  /**
   * @return a struct containing StatNames for router stats.
   */
  virtual const StatNames& statNames() const PURE;

  /**
   * @return a struct containing StatNames for virtual cluster stats.
   */
  virtual const VirtualClusterStatNames& virtualClusterStatNames() const PURE;

  /**
   * @return a struct containing StatNames for route level stats.
   */
  virtual const RouteStatNames& routeStatNames() const PURE;

  /**
   * @return a reference to the default generic connection pool factory.
   */
  virtual GenericConnPoolFactory& genericConnPoolFactory() PURE;
};

/**
 * RouteStatsContext defines config needed to generate all route level stats.
 */
class RouteStatsContext {
public:
  virtual ~RouteStatsContext() = default;

  /**
   * @return stat name for route specific stats.
   */
  virtual Stats::StatName statName() const PURE;

  /**
   * @return a struct containing stats for route stats.
   */
  virtual const RouteStats& stats() const PURE;
};

} // namespace Router
} // namespace Envoy
