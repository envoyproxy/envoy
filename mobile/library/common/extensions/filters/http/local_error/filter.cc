#include "library/common/extensions/filters/http/local_error/filter.h"

#include "envoy/http/codes.h"
#include "envoy/server/filter_config.h"

#include "common/grpc/common.h"
#include "common/grpc/status.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

#include "library/common/http/headers.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

namespace {

// Envoy Mobile surfaces non-"OK" local responses as errors via callbacks rather than an HTTP
// response. Here that response's "real" status code is mapped to an Envoy Mobile error code
// which is passed through the response chain as a header. The status code is updated with
// the sentinel value, 218 ("This is fine").
void mapLocalHttpResponseToError(uint64_t status, Http::ResponseHeaderMap& headers) {
  switch (status) {
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

} // namespace

LocalErrorFilter::LocalErrorFilter() {}

Http::FilterHeadersStatus LocalErrorFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                          bool end_stream) {
  // HACK: currently Envoy sends local replies in cases where an error ought to be
  // surfaced via the error path. See https://github.com/lyft/envoy-mobile/issues/460

  // Absence of EnvoyUpstreamServiceTime header implies this is a local reply, which we potentially
  // map to a stream error.
  if (!headers.get(Http::Headers::get().EnvoyUpstreamServiceTime).empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  const auto grpc_status = Grpc::Common::getGrpcStatus(headers);
  // gRPC status in headers implies this is an error.
  if (grpc_status) {
    ASSERT(end_stream,
           "Local gRPC responses must consist of a single headers frame. If Envoy changes "
           "this expectation, this code needs to be updated.");
    ENVOY_LOG(debug, "intercepted local GRPC response");
    uint64_t http_status = Grpc::Utility::grpcToHttpStatus(grpc_status.value());
    mapLocalHttpResponseToError(http_status, headers);
    headers.addCopy(Http::InternalHeaders::get().ErrorMessage,
                    Grpc::Common::getGrpcMessage(headers));
    return Http::FilterHeadersStatus::Continue;
  }

  uint64_t response_status = Http::Utility::getResponseStatus(headers);
  if (!Http::CodeUtility::is2xx(response_status)) {
    ENVOY_LOG(debug, "intercepted local response");
    httpError_ = true;
    headers_ = &headers;
    mapLocalHttpResponseToError(response_status, headers);
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus LocalErrorFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (httpError_) {
    // We assume the first (and assumed only) data chunk will be a contextual error message.
    ASSERT(end_stream,
           "Local responses must end the stream with a single data frame. If Envoy changes "
           "this expectation, this code needs to be updated.");
    headers_->addCopy(Http::InternalHeaders::get().ErrorMessage, data.toString());
  }

  return Http::FilterDataStatus::Continue;
}

} // namespace LocalError
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
