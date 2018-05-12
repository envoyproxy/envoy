#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

/**
 * Shared logic for evaluating RBAC policies for both network and HTTP filters.
 */
class RBACEngine : public Router::RouteSpecificFilterConfig {
public:
  virtual ~RBACEngine() {}

  /**
   * Returns whether or not the current action is permitted. This overload of `allowed` should only
   * be used for non-HTTP network calls (e.g., a binary TCP protocol).
   *
   * @param connection the downstream connection used to identify the action/principal.
   */
  virtual bool allowed(const Network::Connection& connection) const PURE;

  /**
   * Returns whether or not the current action is permitted. This overload of `allowed` should only
   * be used for HTTP calls.
   *
   * @param connection the downstream connection used to identify the action/principal.
   * @param headers    the headers of the incoming request used to identify the action/principal.
   */
  virtual bool allowed(const Network::Connection& connection,
                       const Envoy::Http::HeaderMap& headers) const PURE;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
