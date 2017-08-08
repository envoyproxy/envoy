#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/json/json_object.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"
#include "common/json/json_validator.h"

namespace Envoy {
namespace Http {

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { Internal, External, Both };

/**
 * Configuration for the ip tagging filter.
 */
class IpTaggingFilterConfig : Json::Validator {
public:
  IpTaggingFilterConfig(const Json::Object& json_config)
      : Json::Validator(json_config, Json::Schema::IP_TAGGING_HTTP_FILTER_SCHEMA),
        request_type_(stringToType(json_config.getString("request_type", "both"))) {}

  FilterRequestType requestType() const { return request_type_; }

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

  const FilterRequestType request_type_;
};

typedef std::shared_ptr<IpTaggingFilterConfig> IpTaggingFilterConfigSharedPtr;

/**
 * A filter that tags requests via the x-envoy-ip-tags header based on the request's trusted XFF
 * address.
 */
class IpTaggingFilter : public StreamDecoderFilter {
public:
  IpTaggingFilter(IpTaggingFilterConfigSharedPtr config);
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

} // namespace Http
} // namespace Envoy
