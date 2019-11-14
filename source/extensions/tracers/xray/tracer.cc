#include "extensions/tracers/xray/tracer.h"

#include <algorithm>
#include <array>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/fmt.h"
#include "common/common/hex.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

void Span::finishSpan() {
  end_time_ = time_source_.systemTime();
  // TODO(marcomagdy): serialize this span and send it to X-Ray Daemon
}

void Span::injectContext(Http::HeaderMap& request_headers) {
  const std::string xray_header_value =
      fmt::format("root={};parent={};sampled=1", traceId(), parent_segment_id_);

  // Set the XRay header in to envoy header map.
  request_headers.setReferenceKey(Http::LowerCaseString(XRayTraceHeader), xray_header_value);
}

Tracing::SpanPtr Tracer::startSpan(const std::string& span_name, const std::string& operation_name,
                                   Envoy::SystemTime start_time,
                                   const absl::optional<XRayHeader>& xray_header) {
  const auto ticks = time_source_.monotonicTime().time_since_epoch().count();
  auto span_ptr = std::make_unique<XRay::Span>(time_source_);
  span_ptr->setId(ticks);
  span_ptr->setName(span_name);
  span_ptr->setOperation(operation_name);
  span_ptr->setStartTime(start_time);
  if (xray_header) { // there's a previous span that this span should be based-on
    span_ptr->setParentId(xray_header->parent_id_);
    span_ptr->setTraceId(xray_header->trace_id_);
    switch (xray_header->sample_decision_) {
    case SamplingDecision::Sampled:
      span_ptr->setSampled(true);
      break;
    case SamplingDecision::NotSampled:
      span_ptr->setSampled(false);
      break;
    default:
      break;
    }
  } else {
    span_ptr->setTraceId(Hex::uint64ToHex(ticks));
  }
  return span_ptr;
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
