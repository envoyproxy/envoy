#include "extensions/tracers/xray/tracer.h"

#include <random>

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/xray_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

const char Tracer::version_[] = "1";
const char Tracer::delimiter_[] = "-";
const char Tracer::hex_digits_[] = "0123456789abcdef";

std::string Tracer::generateRandom96BitString() {
  char values[25] = {'\0'};
  uint64_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      time_source_.systemTime().time_since_epoch())
                      .count();
  std::mt19937 mt_rand(seed);

  for (int i = 0; i < 24; i++) {
    values[i] = hex_digits_[mt_rand() % 16];
  }
  return values;
}

std::string Tracer::generateTraceId() {
  uint64_t epoch_time =
      std::chrono::duration_cast<std::chrono::seconds>(time_source_.systemTime().time_since_epoch())
          .count();
  std::stringstream stream;
  stream << std::hex << epoch_time;
  std::string result(stream.str());
  return absl::StrCat(version_, delimiter_, result, delimiter_, generateRandom96BitString());
}

SpanPtr Tracer::startSpan(const Tracing::Config& config, const std::string& span_name,
                          SystemTime timestamp) {
  // Create an all-new span, with no parent id
  SpanPtr span_ptr(new Span(time_source_));
  span_ptr->setName(span_name);

  // generate a trace id
  span_ptr->setTraceId(generateTraceId());

  uint64_t span_id = random_generator_.random();
  span_ptr->setId(span_id);

  double start_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() /
      static_cast<double>(1000);
  span_ptr->setStartTime(start_time);

  ChildSpan childSpan(time_source_);
  childSpan.setName(span_name);
  uint64_t child_span_id = random_generator_.random();
  childSpan.setId(child_span_id);
  double child_start_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() /
      static_cast<double>(1000);
  childSpan.setStartTime(child_start_time);

  span_ptr->addChildSpan(std::move(childSpan));
  span_ptr->setOperationName(Tracing::HttpTracerUtility::toString(config.operationName()));
  span_ptr->setTracer(this);

  return span_ptr;
}

SpanPtr Tracer::startSpan(const Tracing::Config& config, const std::string& span_name,
                          SystemTime timestamp, SpanContext& previous_context) {
  SpanPtr span_ptr(new Span(time_source_));

  span_ptr->setName(span_name);
  // We need to create a new span and use previous span's id as it's parent id
  uint64_t span_id = random_generator_.random();
  span_ptr->setId(span_id);

  if (previous_context.parent_id()) {
    span_ptr->setParentId(previous_context.parent_id());
  }

  // Keep the same trace id
  span_ptr->setTraceId(previous_context.trace_id());

  // Keep the same sampled flag
  span_ptr->setSampled(previous_context.sampled());

  double start_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() /
      static_cast<double>(1000);
  span_ptr->setStartTime(start_time);

  ChildSpan childSpan(time_source_);
  childSpan.setName(span_name);
  uint64_t child_span_id = random_generator_.random();
  childSpan.setId(child_span_id);
  double child_start_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() /
      static_cast<double>(1000);
  childSpan.setStartTime(child_start_time);
  span_ptr->addChildSpan(std::move(childSpan));
  span_ptr->setOperationName(Tracing::HttpTracerUtility::toString(config.operationName()));
  span_ptr->setTracer(this);

  return span_ptr;
}

void Tracer::reportSpan(Span&& span) {
  if (reporter_ && span.sampled()) {
    reporter_->reportSpan(std::move(span));
  }
}

void Tracer::setReporter(ReporterPtr reporter) { reporter_ = std::move(reporter); }

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
