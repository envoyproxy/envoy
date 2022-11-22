#include "test/integration/filters/http/test_read/filter.h"

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace HttpFilters {
namespace TestRead {

Http::FilterHeadersStatus TestReadFilter::decodeHeaders(Http::RequestHeaderMap& request_headers,
                                                        bool) {
  // sample path is /failed?error=0x10000
  Http::Utility::QueryParams query_parameters =
      Http::Utility::parseQueryString(request_headers.Path()->value().getStringView());
  auto error = query_parameters.find("error");
  uint64_t response_flag;
  if (error != query_parameters.end() && absl::SimpleAtoi(error->second, &response_flag)) {
    // set response error code
    StreamInfo::StreamInfo& stream_info = decoder_callbacks_->streamInfo();
    stream_info.setResponseFlag(TestReadFilter::mapErrorToResponseFlag(response_flag));

    // check if we want a quic server error: sample path is /failed?quic=1&error=0x10000
    if (query_parameters.find("quic") != query_parameters.end()) {
      stream_info.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
      stream_info.upstreamInfo()->setUpstreamProtocol(Http::Protocol::Http3);
    }

    // trigger the error and stop iteration to other filters
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "test_read filter threw: ", nullptr,
                                       absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // continue to other filters since the provided query string is invalid for error simulation
  return Http::FilterHeadersStatus::Continue;
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
