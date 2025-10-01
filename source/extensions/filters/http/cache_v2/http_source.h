#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/cache_v2/range_utils.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

// Reset indicates that the upstream source reset (or, if it's not a stream, some
// kind of unexpected error).
// More is equivalent to bool end_stream=false.
// End is equivalent to bool end_stream=true.
enum class EndStream { Reset, More, End };
using GetHeadersCallback =
    absl::AnyInvocable<void(Http::ResponseHeaderMapPtr headers, EndStream end_stream)>;
using GetBodyCallback = absl::AnyInvocable<void(Buffer::InstancePtr buffer, EndStream end_stream)>;
using GetTrailersCallback =
    absl::AnyInvocable<void(Http::ResponseTrailerMapPtr trailers, EndStream end_stream)>;

// HttpSource is an interface for a source of HTTP data.
// Callbacks can potentially be called before returning from the get* function.
// The callback should be called on the same thread as the caller.
// Only one request should be in flight at a time, and requests must be in
// order as the source is assumed to be a stream (i.e. headers before body,
// earlier body before later body, trailers last).
class HttpSource {
public:
  // Calls the provided callback with http headers.
  virtual void getHeaders(GetHeadersCallback&& cb) PURE;
  // Calls the provided callback with a buffer that is the beginning of the
  // requested range, up to but not necessarily including the entire requested
  // range, or no buffer if there is no more data or an error occurred.
  virtual void getBody(AdjustedByteRange range, GetBodyCallback&& cb) PURE;
  virtual void getTrailers(GetTrailersCallback&& cb) PURE;
  virtual ~HttpSource() = default;
};

using HttpSourcePtr = std::unique_ptr<HttpSource>;

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
