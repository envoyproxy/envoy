#pragma once

#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/tracing/tracer.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/common/tracing/tracer_impl.h"

namespace Envoy {
namespace Tracing {

class HttpTracerUtility {
public:
  /**
   * Adds information obtained from the downstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeDownstreamSpan(Span& span, const Http::RequestHeaderMap* request_headers,
                                     const Http::ResponseHeaderMap* response_headers,
                                     const Http::ResponseTrailerMap* response_trailers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     const Config& tracing_config);

  /**
   * Adds information obtained from the upstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeUpstreamSpan(Span& span, const StreamInfo::StreamInfo& stream_info,
                                   const Config& tracing_config);

  /**
   * Adds tags to the current "unfinished" span when processing upstream response headers.
   * NOOP if headers are nullptr.
   */
  static void onUpstreamResponseHeaders(Span& span,
                                        const Http::ResponseHeaderMap* response_headers);

  /**
   * Adds tags to the current "unfinished" span when processing upstream response trailers.
   * NOOP if trailers are nullptr.
   */
  static void onUpstreamResponseTrailers(Span& span,
                                         const Http::ResponseTrailerMap* response_trailers);

private:
  static void setCommonTags(Span& span, const StreamInfo::StreamInfo& stream_info,
                            const Config& tracing_config);
};

} // namespace Tracing
} // namespace Envoy
