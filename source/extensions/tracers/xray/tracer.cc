#include "extensions/tracers/xray/tracer.h"

#include <chrono>
#include <ctime>

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/xray_core_constants.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                const std::string Tracer::VERSION_ = "1";
                const std::string Tracer::DELIMITER_ = "-";

                std::string Tracer::random_24bits_string() {
                    char values[25] = {'\0'};
                    srand(static_cast<unsigned int> (time(NULL)));
                    for (int i = 0; i < 24; i++) {
                        values[i] = hex_digits[rand() % 16];
                    }
                    return values;
                }

                std::string Tracer::generateTraceId() {
                    time_t epoch = time(nullptr);
                    std::stringstream stream;
                    stream << std::hex << epoch;
                    std::string result(stream.str());
                    std::string trace_id = VERSION_ + DELIMITER_ + result + DELIMITER_ + random_24bits_string();
                    return trace_id;
                }

                SpanPtr Tracer::startSpan(const Tracing::Config &config, const std::string &span_name, SystemTime timestamp) {
                    config.operationName();
                    timestamp.time_since_epoch();

                    // Create an all-new span, with no parent id
                    SpanPtr span_ptr(new Span());
                    span_ptr->setName(span_name);

                    // generate a trace id
                    span_ptr->setTraceId(generateTraceId());

                    uint64_t span_id = random_generator_.random();
                    span_ptr->setId(span_id);

                    double start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    span_ptr->setStartTime(start_time);

                    ChildSpan childSpan;
                    childSpan.setName(span_name);
                    uint64_t child_span_id = random_generator_.random();
                    childSpan.setId(child_span_id);
                    double child_start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    childSpan.setStartTime(child_start_time);
                    
                    span_ptr->addChildSpan(std::move(childSpan));

                    span_ptr->setTracer(this);

                    return span_ptr;
                }

                SpanPtr Tracer::startSpan(const Tracing::Config &config, const std::string &span_name, SystemTime timestamp, SpanContext &previous_context) {
                    timestamp.time_since_epoch();
		            SpanPtr span_ptr(new Span());

                    span_ptr->setName(span_name);

                    if (config.operationName() == Tracing::OperationName::Egress) {
                        // We need to create a new span that is a child of the previous span; no shared context

                        // Create a new span id
                        uint64_t span_id = random_generator_.random();
                        span_ptr->setId(span_id);

                        // Set the parent id to the id of the previous span
                        span_ptr->setParentId(previous_context.parent_id());
                    } else if (config.operationName() == Tracing::OperationName::Ingress) {
                        // We need to create a new span and use previous span's id as it's parent id
                        uint64_t span_id = random_generator_.random();
                        span_ptr->setId(span_id);

                        if (previous_context.parent_id()) {
                            span_ptr->setParentId(previous_context.parent_id());
                        }
                    } else {
                        return span_ptr; // return an empty span
                    }

                    // Keep the same trace id
                    span_ptr->setTraceId(previous_context.trace_id());

                    // Keep the same sampled flag
                    span_ptr->setSampled(previous_context.sampled());

                    double start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    span_ptr->setStartTime(start_time);

                    ChildSpan childSpan;
                    childSpan.setName(span_name);
                    uint64_t child_span_id = random_generator_.random();
                    childSpan.setId(child_span_id);
                    double child_start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    childSpan.setStartTime(child_start_time);
                    span_ptr->addChildSpan(std::move(childSpan));

                    span_ptr->setTracer(this);

                    return span_ptr;
                }

                void Tracer::reportSpan(Span&& span) {
                    if (reporter_ && span.sampled()) {
                        reporter_->reportSpan(std::move(span));
                    }
                }

                void Tracer::setReporter(ReporterPtr reporter) { reporter_ = std::move(reporter); }
            }
        }
    }
}
