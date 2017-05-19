#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/json/json_object.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"
#include "common/json/json_validator.h"

namespace Envoy {
namespace Http {

/**
 * All stats for the ip tagging filter. @see stats_macros.h
 */
// clang-format off
#define ALL_IP_TAGGING_FILTER_STATS(COUNTER)                                                            \
  COUNTER(placeholder)                                                                         \
// clang-format on

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct IpTaggingFilterStats {
  ALL_IP_TAGGING_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType {
  Internal, External, Both
};

/**
 * Configuration for the ip tagging filter.
 */
class IpTaggingFilterConfig : Json::Validator {
public:
  IpTaggingFilterConfig(const Json::Object& json_config, Runtime::Loader& runtime,
                        const std::string& stat_prefix, Stats::Store& stats)
    : Json::Validator(json_config, Json::Schema::IP_TAGGING_HTTP_FILTER_SCHEMA),
      runtime_(runtime), stats_(generateStats(stat_prefix, stats)),
      request_type_(stringToType(json_config.getString("request_type", "both"))) {}

  FilterRequestType requestType() const { return request_type_; }
  //const Trie& ipTags() { return ip_tags_; }
  Runtime::Loader &runtime() { return runtime_; }
  IpTaggingFilterStats &stats() { return stats_; }

private:
  static FilterRequestType stringToType(const std::string& request_type) {
    if (request_type == "internal") {
      return FilterRequestType::Internal;
    } else if (request_type == "external") {
      return FilterRequestType::External;
    } else {
      ASSERT(request_type == "both");
      return FilterRequestType::Both;
    }
  }

  static IpTaggingFilterStats generateStats(const std::string& prefix, Stats::Store& store);

  //Trie ip_tags_;
  Runtime::Loader& runtime_;
  IpTaggingFilterStats stats_;
  const FilterRequestType request_type_;
};

typedef std::shared_ptr<IpTaggingFilterConfig> IpTaggingFilterConfigSharedPtr;

/**
 * A filter that tags requests via the x-envoy-ip-tags header based on the request's trusted XFF address.
 */
class IpTaggingFilter : public StreamDecoderFilter {
public:
  IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {}

  ~IpTaggingFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

private:
  IpTaggingFilterConfigSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
};

} // Http
} // Envoy