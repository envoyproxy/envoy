#pragma once

#include <memory>

#include "envoy/extensions/http/ai_filters/schema_validation/v3/schema_validation.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {

#define ALL_SCHEMA_VALIDATION_FILTER_STATS(COUNTER)                                                \
  COUNTER(valid)                                                                                   \
  COUNTER(invalid)                                                                                 \
  COUNTER(skipped)                                                                                 \
  COUNTER(llm_protocol_detected)

struct SchemaValidationFilterStats {
  ALL_SCHEMA_VALIDATION_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class SchemaValidationFilterConfig {
public:
  SchemaValidationFilterConfig(
      const envoy::extensions::http::ai_filters::schema_validation::v3::SchemaValidation& proto,
      Stats::Scope& scope);

  // Unspecified means detect.
  HttpFilters::AiProtocolManager::LLMProtocol defaultLlmProtocol() const {
    return default_llm_protocol_;
  }
  bool failOpen() const { return fail_open_; }
  const SchemaValidationFilterStats& stats() const { return stats_; }

private:
  const SchemaValidationFilterStats stats_;
  const HttpFilters::AiProtocolManager::LLMProtocol default_llm_protocol_;
  const bool fail_open_;
};
using SchemaValidationFilterConfigSharedPtr = std::shared_ptr<const SchemaValidationFilterConfig>;

// Resolves the request's wire API onto the AiRequest and holds the payload to that API's schema.
class SchemaValidationFilter : public Common::SyncAiFilter,
                               public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  SchemaValidationFilter(SchemaValidationFilterConfigSharedPtr config,
                         const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

private:
  HttpFilters::AiProtocolManager::LLMProtocol
  resolveLlmProtocol(const HttpFilters::AiProtocolManager::AiRequest& request) const;

  SchemaValidationFilterConfigSharedPtr config_;
  const HttpFilters::AiProtocolManager::AiFilterContext context_;
};

} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
