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
   * Handles action-specific operations and returns whether or not the request is permitted.
   *
   * @param connection the downstream connection used to identify the action/principal.
   * @param headers    the headers of the incoming request used to identify the action/principal. An
   *                   empty map should be used if there are no headers available.
   * @param info       the per-request or per-connection stream info with additional information
   *                   about the action/principal. Can be modified by the LOG Action.
   * @param effective_policy_id  it will be filled by the matching policy's ID,
   *                   which is used to identity the source of the allow/deny.
   */
  virtual bool handleAction(const Network::Connection& connection,
                            const Envoy::Http::RequestHeaderMap& headers,
                            StreamInfo::StreamInfo& info,
                            std::string* effective_policy_id) const PURE;

  /**
   * Handles action-specific operations and returns whether or not the request is permitted.
   *
   * @param connection the downstream connection used to identify the action/principal.
   * @param info       the per-request or per-connection stream info with additional information
   *                   about the action/principal. Can be modified by the LOG Action.
   * @param effective_policy_id  it will be filled by the matching policy's ID,
   *                   which is used to identity the source of the allow/deny.
   */
  virtual bool handleAction(const Network::Connection& connection, StreamInfo::StreamInfo& info,
                            std::string* effective_policy_id) const PURE;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
