#pragma once

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/common/http/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/common/http/filters/test_read/filter.pb.h"

namespace Envoy {
namespace HttpFilters {
namespace TestRead {

/**
 * This is a test-only filter to return specified error code based on a request url query string.
 * It either simulates the requested error if the url query matches the error patterns
 * or does nothing
 */
class TestReadFilter final : public Http::PassThroughFilter,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override;

private:
  /* A mapping of the envoymobile errors we care about for testing
   * From https://github.com/envoyproxy/envoy/blob/main/envoy/stream_info/stream_info.h
   */
  StreamInfo::CoreResponseFlag mapErrorToResponseFlag(uint64_t errorCode);
};

} // namespace TestRead
} // namespace HttpFilters
} // namespace Envoy
