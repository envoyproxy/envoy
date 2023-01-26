#pragma once

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "library/common/extensions/filters/http/test_remote_response/filter.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestRemoteResponse {

/**
 * Filter to assert expectations on HTTP requests.
 */
  class TestRemoteResponseFilter final : public Http::PassThroughFilter,
  public Logger::Loggable<Logger::Id::filter> {
   public:
    // StreamFilterBase
    Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&,
                                            bool end_stream) override;
    Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override;
    Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;

    void sendResponse();
  };

} // namespace TestRemoteResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
