#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

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
  virtual ~RoleBasedAccessControlEngine() = default;

  /**
   * Returns whether or not the current action is permitted.
   *
   * @param connection the downstream connection used to identify the action/principal.
   * @param headers    the headers of the incoming request used to identify the action/principal. An
   *                   empty map should be used if there are no headers available.
   * @param info       the per-request or per-connection stream info with additional information
   *                   about the action/principal.
   * @param effective_policy_id  it will be filled by the matching policy's ID,
   *                   which is used to identity the source of the allow/deny.
   */
  virtual bool allowed(const Network::Connection& connection,
                       const Envoy::Http::RequestHeaderMap& headers,
                       const StreamInfo::StreamInfo& info,
                       std::string* effective_policy_id) const PURE;

  /**
   * Returns whether or not the current action is permitted.
   *
   * @param connection the downstream connection used to identify the action/principal.
   * @param info       the per-request or per-connection stream info with additional information
   *                   about the action/principal.
   * @param effective_policy_id  it will be filled by the matching policy's ID,
   *                   which is used to identity the source of the allow/deny.
   */
  virtual bool allowed(const Network::Connection& connection, const StreamInfo::StreamInfo& info,
                       std::string* effective_policy_id) const PURE;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
