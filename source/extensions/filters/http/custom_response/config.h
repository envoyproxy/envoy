#pragma once

#include <memory>

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/http/matching/inputs.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/custom_response/response.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

/**
 * All stats for the custom response filter. @see stats_macros.h
 */
#define ALL_CUSTOM_RESPONSE_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)        \
  COUNTER(custom_response_redirect_no_route)                                                       \
  COUNTER(custom_response_redirect_invalid_uri)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(CustomResponseFilterStatNames, ALL_CUSTOM_RESPONSE_FILTER_STATS);
MAKE_STATS_STRUCT(CustomResponseFilterStats, CustomResponseFilterStatNames,
                  ALL_CUSTOM_RESPONSE_FILTER_STATS);

/**
 * Container class to store filter configuration, which includes custom
 * responses, and matching tree/list to get custom response for a particular
 * upstream response.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
               Stats::StatName prefix, Server::Configuration::FactoryContext& context);

  ResponseSharedPtr getResponse(Http::ResponseHeaderMap& headers,
                                const StreamInfo::StreamInfo& stream_info);

  CustomResponseFilterStats& stats() { return stats_; }

private:
  CustomResponseFilterStatNames stat_names_;
  CustomResponseFilterStats stats_;
  absl::flat_hash_map<absl::string_view, ResponseSharedPtr> responses_;
  Matcher::MatchTreePtr<Http::HttpMatchingData> matcher_;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
