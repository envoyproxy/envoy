#pragma once

#include "source/extensions/filters/common/rbac/matching/data.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matching {

/**
 * Implementation of RBAC::MatchingData, providing network, connection and stream data to the match
 * tree.
 */
class MatchingDataImpl : public MatchingData {
public:
  MatchingDataImpl(const Network::Connection& connection,
                   const Envoy::Http::RequestHeaderMap& headers,
                   const StreamInfo::StreamInfo& stream_info)
      : connection_(connection), headers_(headers), stream_info_(stream_info) {}

  const Network::Connection& connection() const override { return connection_; }
  const Envoy::Http::RequestHeaderMap& headers() const override { return headers_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }

private:
  const Network::Connection& connection_;
  const Envoy::Http::RequestHeaderMap& headers_;
  const StreamInfo::StreamInfo& stream_info_;
};

} // namespace Matching
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
