#pragma once

#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matching {

/**
 * RBAC filter matching context data for unified matchers.
 */
class MatchingData {
public:
  static absl::string_view name() { return "rbac"; }

  virtual ~MatchingData() = default;

  virtual const Network::Connection& connection() const PURE;
  virtual const Envoy::Http::RequestHeaderMap& headers() const PURE;
  virtual const StreamInfo::StreamInfo& streamInfo() const PURE;

  const Network::ConnectionInfoProvider& connectionInfoProvider() const {
    return connection().connectionInfoProvider();
  }

  Http::RequestHeaderMapOptConstRef requestHeaders() const { return headers(); }
};

} // namespace Matching
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
