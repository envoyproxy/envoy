#include "source/extensions/tracers/zipkin/span_context.h"

#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

SpanContext::SpanContext(const Span& span, bool inner_context)
    : trace_id_high_(span.isSetTraceIdHigh() ? span.traceIdHigh() : 0), trace_id_(span.traceId()),
      id_(span.id()), parent_id_(span.isSetParentId() ? span.parentId() : 0),
      sampled_(span.sampled()), inner_context_(inner_context) {}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
