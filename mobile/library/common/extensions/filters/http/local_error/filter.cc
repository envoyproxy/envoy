#include "library/common/extensions/filters/http/local_error/filter.h"

#include "envoy/http/codes.h"
#include "envoy/server/filter_config.h"

#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

#include "library/common/http/headers.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

LocalErrorFilter::LocalErrorFilter() {}

Http::FilterHeadersStatus LocalErrorFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                          bool end_stream) {
  uint64_t response_status = Http::Utility::getResponseStatus(headers);
  bool success = Http::CodeUtility::is2xx(response_status);

  // TODO: ***HACK*** currently Envoy sends local replies in cases where an error ought to be
  // surfaced via the error path. There are ways we can clean up Envoy's local reply path to
  // make this possible, but nothing expedient. For the immediate term this is our only real
  // option. See https://github.com/lyft/envoy-mobile/issues/460

  // Absence of EnvoyUpstreamServiceTime header implies this is a local reply, which we treat as
  // a stream error.
  if (!success && headers.get(Http::Headers::get().EnvoyUpstreamServiceTime).empty()) {
    ENVOY_LOG(debug, "intercepted local response");
    processingError_ = true;
    headers_ = &headers;
    mapLocalResponseToError(headers);
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus LocalErrorFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (processingError_) {
    // We assume the first (and assumed only) data chunk will be a contextual error message.
    ASSERT(end_stream,
           "Local responses must end the stream with a single data frame. If Envoy changes "
           "this expectation, this code needs to be updated.");
    headers_->addCopy(Http::InternalHeaders::get().ErrorMessage, data.toString());
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::Continue;
}

void LocalErrorFilter::mapLocalResponseToError(Http::ResponseHeaderMap& headers) {
  // Envoy Mobile surfaces non-200 local responses as errors via callbacks rather than an HTTP
  // response. Here that response's "real" status code is mapped to an Envoy Mobile error code
  // which is passed through the response chain as a header. The status code is updated with
  // the sentinel value, 218 ("This is fine").
  switch (Http::Utility::getResponseStatus(headers)) {
  case 408:
    headers.addCopy(Http::InternalHeaders::get().ErrorCode, std::to_string(ENVOY_REQUEST_TIMEOUT));
    break;
  case 413:
    headers.addCopy(Http::InternalHeaders::get().ErrorCode,
                    std::to_string(ENVOY_BUFFER_LIMIT_EXCEEDED));
    break;
  case 503:
    headers.addCopy(Http::InternalHeaders::get().ErrorCode,
                    std::to_string(ENVOY_CONNECTION_FAILURE));
    break;
  default:
    headers.addCopy(Http::InternalHeaders::get().ErrorCode, std::to_string(ENVOY_UNDEFINED_ERROR));
  }

  headers.setStatus(218);
}

} // namespace LocalError
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
