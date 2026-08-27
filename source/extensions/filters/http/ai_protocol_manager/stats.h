#pragma once

#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

/**
 * All stats for the AI Protocol Manager filter. @see stats_macros.h
 */
#define ALL_AI_PROTOCOL_MANAGER_STATS(COUNTER)                                                     \
  COUNTER(token_usage_found)                                                                       \
  COUNTER(token_usage_partial)                                                                     \
  COUNTER(token_usage_failed)                                                                      \
  COUNTER(token_usage_missing)                                                                     \
  COUNTER(token_usage_total_mismatch)                                                              \
  COUNTER(token_usage_duplicate)                                                                   \
  COUNTER(response_parse_error)                                                                    \
  COUNTER(response_body_too_large)                                                                 \
  COUNTER(malformed_usage_field)                                                                   \
  COUNTER(sse_event_too_large)                                                                     \
  COUNTER(sse_incomplete_event)                                                                    \
  COUNTER(sse_event_budget_exhausted)                                                              \
  COUNTER(unsupported_content_encoding)

struct AiProtocolManagerStats {
  ALL_AI_PROTOCOL_MANAGER_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
