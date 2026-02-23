#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/a2a/v3/a2a.pb.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/a2a/a2a_json_parser.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

/**
 * All A2A filter stats. @see stats_macros.h
 */
#define A2A_FILTER_STATS(COUNTER)                                                                  \
  COUNTER(requests_rejected)                                                                       \
  COUNTER(invalid_json)                                                                            \
  COUNTER(body_too_large)

/**
 * Struct definition for A2A filter stats. @see stats_macros.h
 */
struct A2aFilterStats {
  A2A_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the A2A filter.
 */
class A2aFilterConfig {
public:
  A2aFilterConfig(const envoy::extensions::filters::http::a2a::v3::A2a& proto_config,
                  const std::string& stats_prefix, Stats::Scope& scope);

  envoy::extensions::filters::http::a2a::v3::A2a::TrafficMode trafficMode() const {
    return traffic_mode_;
  }

  uint32_t maxRequestBodySize() const { return max_request_body_size_; }
  const A2aParserConfig& parserConfig() const { return parser_config_; }

  A2aFilterStats& stats() { return stats_; }

private:
  const envoy::extensions::filters::http::a2a::v3::A2a::TrafficMode traffic_mode_;
  const uint32_t max_request_body_size_;
  A2aParserConfig parser_config_;
  A2aFilterStats stats_;
};

using A2aFilterConfigSharedPtr = std::shared_ptr<A2aFilterConfig>;

/**
 * A2A filter implementation.
 */
class A2aFilter : public Http::PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  explicit A2aFilter(A2aFilterConfigSharedPtr config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

private:
  bool isValidA2aGetOrDeleteRequest(const Http::RequestHeaderMap& headers) const;
  bool isValidA2aPostRequest(const Http::RequestHeaderMap& headers) const;

  bool shouldRejectRequest() const;
  uint32_t getMaxRequestBodySize() const;

  const A2aFilterConfigSharedPtr config_;
  std::unique_ptr<A2aJsonParser> parser_;
  bool is_a2a_request_{false};
  bool is_json_post_request_{false};
};

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
