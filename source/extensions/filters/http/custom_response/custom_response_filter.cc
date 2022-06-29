#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

Http::FilterHeadersStatus CustomResponseFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  (void)headers;
  (void)end_stream;
  // TODO: logic here to traverse universal matcher to find match for custom
  // response. Have separate implementation function to be called from
  // send local reply?
  return Http::FilterHeadersStatus::Continue;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
