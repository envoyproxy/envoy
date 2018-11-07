#include "extensions/tracers/xray/span_context.h"

#include "common/common/macros.h"
#include "common/common/utility.h"

#include "extensions/tracers/xray/xray_core_constants.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                SpanContext::SpanContext(const Span& span)
                        : trace_id_(span.traceId()), id_(span.id()), parent_id_(span.isSetParentId() ? span.parentId() : 0), sampled_(span.sampled()) {}

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
