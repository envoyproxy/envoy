#include "test/integration/filters/http/test_read/filter.h"

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace HttpFilters {
namespace TestRead {

Http::FilterHeadersStatus TestReadFilter::decodeHeaders(Http::RequestHeaderMap& request_headers,
                                                        bool) {
  // sample path is /failed?start=0x10000
  Http::Utility::QueryParams query_parameters =
      Http::Utility::parseQueryString(request_headers.Path()->value().getStringView());
  uint64_t response_flag;
  if (absl::SimpleAtoi(query_parameters.at("start"), &response_flag)) {
    decoder_callbacks_->streamInfo().setResponseFlag(
        TestReadFilter::mapErrorToResponseFlag(response_flag));
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "test_read filter threw: ", nullptr,
                                       absl::nullopt, "");
  }
  return Http::FilterHeadersStatus::StopIteration;
}

StreamInfo::ResponseFlag TestReadFilter::mapErrorToResponseFlag(uint64_t errorCode) {
  switch (errorCode) {
  case 0x4000000:
    return StreamInfo::DnsResolutionFailed;
  case 0x40:
    return StreamInfo::UpstreamConnectionTermination;
  case 0x20:
    return StreamInfo::UpstreamConnectionFailure;
  case 0x10:
    return StreamInfo::UpstreamRemoteReset;
  case 0x10000:
    return StreamInfo::StreamIdleTimeout;
  default:
    // Any other error that we aren't interested in. I picked a random error.
    return StreamInfo::RateLimitServiceError;
  }
}

} // namespace TestRead
} // namespace HttpFilters
} // namespace Envoy
