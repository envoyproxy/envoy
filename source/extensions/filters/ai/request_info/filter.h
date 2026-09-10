#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/ai/request_info/v3/request_info.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

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
  RequestInfoFilterConfig(const envoy::extensions::filters::ai::request_info::v3::RequestInfo& proto,
                          Stats::Scope& scope);

  const std::string& metadataNamespace() const { return metadata_namespace_; }
  RequestInfoFilterStats& stats() const { return stats_; }

private:
  mutable RequestInfoFilterStats stats_;
  const std::string metadata_namespace_;
};
using RequestInfoFilterConfigSharedPtr = std::shared_ptr<const RequestInfoFilterConfig>;

// Publishes envoy.data.ai.v3.RequestInfo before the manager releases the request headers, so
// later decode filters see it from their first headers callback. First writer owns the namespace.
class RequestInfoFilter : public HttpFilters::AiProtocolManager::AiFilter,
                          public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  RequestInfoFilter(RequestInfoFilterConfigSharedPtr config,
                    const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // HttpFilters::AiProtocolManager::AiFilter
  Coroutine::Task<absl::Status>
  decode(HttpFilters::AiProtocolManager::AiRequestReceiver receive_request,
         HttpFilters::AiProtocolManager::AiRequestPropagator propagate_request,
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
