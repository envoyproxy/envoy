#include "source/server/admin/utils.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"

namespace Envoy {
namespace Server {
namespace Utility {

void populateFallbackResponseHeaders(Http::Code code, Http::ResponseHeaderMap& header_map) {
  header_map.setStatus(std::to_string(enumToInt(code)));
  if (header_map.ContentType() == nullptr) {
    // Default to text-plain if unset.
    header_map.setReferenceContentType(Http::Headers::get().ContentTypeValues.TextUtf8);
  }
  // Default to 'no-cache' if unset, but not 'no-store' which may break the back button.
  if (header_map.get(Http::CustomHeaders::get().CacheControl).empty()) {
    header_map.setReference(Http::CustomHeaders::get().CacheControl,
                            Http::CustomHeaders::get().CacheControlValues.NoCacheMaxAge0);
  }

  // Under no circumstance should browsers sniff content-type.
  header_map.addReference(Http::Headers::get().XContentTypeOptions,
                          Http::Headers::get().XContentTypeOptionValues.Nosniff);
}

// Helper method to get the histogram_buckets parameter. Returns an InvalidArgumentError
// if histogram_buckets query param is found and value is not "cumulative" or "disjoint",
// Ok otherwise.
absl::Status histogramBucketsParam(const Http::Utility::QueryParamsMulti& params,
                                   HistogramBucketsMode& histogram_buckets_mode) {
  absl::optional<std::string> histogram_buckets_query_param =
      nonEmptyQueryParam(params, "histogram_buckets");
  histogram_buckets_mode = HistogramBucketsMode::Unset;
  if (histogram_buckets_query_param.has_value()) {
    if (histogram_buckets_query_param.value() == "cumulative") {
      histogram_buckets_mode = HistogramBucketsMode::Cumulative;
    } else if (histogram_buckets_query_param.value() == "disjoint") {
      histogram_buckets_mode = HistogramBucketsMode::Disjoint;
    } else if (histogram_buckets_query_param.value() == "detailed") {
      histogram_buckets_mode = HistogramBucketsMode::Detailed;
      // "none" is a synonym for "summary", and exists to maintain backwards compatibility
    } else if (histogram_buckets_query_param.value() == "summary" ||
               histogram_buckets_query_param.value() == "none") {
      histogram_buckets_mode = HistogramBucketsMode::Summary;
    } else {
      return absl::InvalidArgumentError(
          "usage: /stats?histogram_buckets=(cumulative|disjoint|detailed|summary)\n");
    }
  }
  return absl::OkStatus();
}

// Helper method to get a query parameter.
// Returns the first value for that query parameter, unless that value is empty.
// In that case, it returns nullopt.
absl::optional<std::string> nonEmptyQueryParam(const Http::Utility::QueryParamsMulti& params,
                                               const std::string& key) {
  const auto data = params.getFirstValue(key);
  if (data.has_value() && data.value().empty()) {
    return absl::nullopt;
  }
  return data;
}

// Helper method to get the format parameter.
absl::optional<std::string> formatParam(const Http::Utility::QueryParamsMulti& params) {
  return nonEmptyQueryParam(params, "format");
}

} // namespace Utility
} // namespace Server
} // namespace Envoy
