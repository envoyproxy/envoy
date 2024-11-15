#pragma once

#include "envoy/common/exception.h"
#include "envoy/tracing/tracer.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

class SpanContext;

struct ExtractorException : public EnvoyException {
  ExtractorException(const std::string& what) : EnvoyException(what) {}
};

/**
 * This class is used to SpanContext extracted from the Http header
 */
class SpanContextExtractor {
public:
  SpanContextExtractor(Tracing::TraceContext& trace_context);
  ~SpanContextExtractor();
  bool extractSampled(const Tracing::Decision tracing_decision);
  std::pair<SpanContext, bool> extractSpanContext(bool is_sampled);

private:
  /*
   * Use to SpanContext extracted from B3 single format Http header
   * b3: {x-b3-traceid}-{x-b3-spanid}-{if x-b3-flags 'd' else x-b3-sampled}-{x-b3-parentspanid}
   * See: "https://github.com/openzipkin/b3-propagation
   */
  std::pair<SpanContext, bool> extractSpanContextFromB3SingleFormat(bool is_sampled);
  bool tryExtractSampledFromB3SingleFormat();
  const Tracing::TraceContext& trace_context_;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
