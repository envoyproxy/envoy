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
 * Shared logic for evaluating RBAC policies.
 */
class RoleBasedAccessControlEngine {
public:
  virtual ~RoleBasedAccessControlEngine() {}

  /**
   * Returns whether or not the current action is permitted.
   *
   * @param connection the downstream connection used to identify the action/principal.
   * @param headers    the headers of the incoming request used to identify the action/principal. An
   *                   empty map should be used if there are no headers available.
   */
  virtual bool allowed(const Network::Connection& connection,
                       const Envoy::Http::HeaderMap& headers) const PURE;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
