#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/runtime/runtime.h"
#include "envoy/tracing/http_tracer.h"

#include "extensions/tracers/xray/span_context.h"
#include "extensions/tracers/xray/tracer_interface.h"
#include "extensions/tracers/xray/xray_core_constants.h"
#include "extensions/tracers/xray/xray_core_types.h"
#include "extensions/tracers/xray/sampling.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                /**
                 * Abstract class that delegates to users of the Tracer class the responsibility
                 * of "reporting" a xray span to xray daemon.
                 */
                class Reporter {
                public:
                  /**
                   * Destructor.
                   */
                  virtual ~Reporter() {}

                  /**
                   * Method that a concrete Reporter class must implement to handle finished spans.
                   *
                   * @param span The span that needs action.
                   */
                  virtual void reportSpan(const Span& span) PURE;
                };

                typedef std::unique_ptr<Reporter> ReporterPtr;

                /**
                 * This class implements the XRay tracer.
                 */
                class Tracer : public TracerInterface {
                public:
                    /**
                     * Constructor.
                     *
                     * @param segment_name The name of the X-Ray segment.
                     * @param random_generator Reference to the random-number generator to be used by the Tracer.
                     */
                    Tracer(const std::string& segment_name, Runtime::RandomGenerator& random_generator, LocalizedSamplingStrategy& localized_sampling_strategy) : segment_name_(
                            segment_name), reporter_(nullptr), random_generator_(random_generator), localized_sampling_strategy_(localized_sampling_strategy) {}

                    /**
                     * Creates a "root" XRay span.
                     *
                     * @param config The tracing configuration
                     * @param span_name Name of the new span.
                     * @param start_time The time indicating the beginning of the span.
                     */
                    SpanPtr startSpan(const Tracing::Config&, const std::string& span_name, SystemTime timestamp);

                    /**
                     * Depending on the given context, creates either a "child" or a "shared-context" XRay span.
                     *
                     * @param config The tracing configuration
                     * @param span_name Name of the new span.
                     * @param start_time The time indicating the beginning of the span.
                     * @param previous_context The context of the span preceding the one to be created.
                     */
                    SpanPtr startSpan(const Tracing::Config&, const std::string& span_name, SystemTime timestamp,
                                      SpanContext& previous_context);

                    /**
                     * TracerInterface::reportSpan.
                     */
                    void reportSpan(Span&& span) override;

                    /**
                     * Associates a Reporter object with this Tracer.
                     */
                    void setReporter(ReporterPtr reporter);

                    /**
                     * @return the segment name associated with the Tracer.
                     */
                    const std::string& segmentName() const { return segment_name_; }

                    /**
                     * @return the random-number generator associated with the Tracer.
                     */
                    Runtime::RandomGenerator& randomGenerator() { return random_generator_; }

                    /**
                     *
                     * @return the instance of localized sampling strategy.
                     */
                    LocalizedSamplingStrategy& localizedSamplingStrategy() { return localized_sampling_strategy_; }

                    /**
                     * Generates A 96-bit identifier for the trace, globally unique, in 24 hexadecimal digits.
                     */
                    std::string generateRandom96BitString();

                    /**
                     * Generates A unique identifier that connects all segments and subsegments originating from a single client request.
                     */
                    std::string generateTraceId();

                private:
                    static const std::string VERSION_;
                    static const std::string DELIMITER_;
                    const char *hex_digits = "0123456789abcdef";

                    const std::string segment_name_;
                    ReporterPtr reporter_;
                    Runtime::RandomGenerator& random_generator_;
                    LocalizedSamplingStrategy localized_sampling_strategy_;
                };

                typedef std::unique_ptr<Tracer> TracerPtr;
            }
        }
    }
}