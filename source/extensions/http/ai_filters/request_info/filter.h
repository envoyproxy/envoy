#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/extensions/http/ai_filters/request_info/v3/request_info.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"
#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

#define ALL_REQUEST_INFO_FILTER_STATS(COUNTER)                                                     \
  COUNTER(published)                                                                               \
  COUNTER(partial)                                                                                 \
  COUNTER(duplicate)

struct RequestInfoFilterStats {
  ALL_REQUEST_INFO_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class RequestInfoFilterConfig {
public:
  RequestInfoFilterConfig(
      const envoy::extensions::http::ai_filters::request_info::v3::RequestInfo& proto,
      Stats::Scope& scope);

  const std::string& metadataNamespace() const { return metadata_namespace_; }
  // Empty when no estimate is configured.
  const std::optional<double>& tokensPerByte() const { return tokens_per_byte_; }
  const RequestInfoFilterStats& stats() const { return stats_; }

private:
  const RequestInfoFilterStats stats_;
  const std::string metadata_namespace_;
  const std::optional<double> tokens_per_byte_;
};
using RequestInfoFilterConfigSharedPtr = std::shared_ptr<const RequestInfoFilterConfig>;

// Publishes envoy.data.ai.v3.RequestInfo before the manager releases the request headers, so
// later decode filters see it from their first headers callback. First writer owns the namespace.
class RequestInfoFilter : public Common::SyncAiFilter,
                          public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  RequestInfoFilter(RequestInfoFilterConfigSharedPtr config,
                    const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

private:
  void publish(const nlohmann::json& json);

  RequestInfoFilterConfigSharedPtr config_;
  const HttpFilters::AiProtocolManager::AiFilterContext context_;
};

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
