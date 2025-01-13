#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/cache/range_utils.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

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

class HttpSourceFactory {
public:
  virtual std::unique_ptr<HttpSource> create() PURE;
  virtual ~HttpSourceFactory() = default;
};

using HttpSourcePtr = std::unique_ptr<HttpSource>;
using HttpSourceFactoryPtr = std::unique_ptr<HttpSourceFactory>;

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
