#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/runtime/runtime.h"
#include "envoy/tracing/http_tracer.h"

#include "extensions/tracers/xray/span_context.h"
#include "extensions/tracers/xray/tracer_interface.h"
#include "extensions/tracers/xray/xray_core_constants.h"
#include "extensions/tracers/xray/xray_core_types.h"

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

                class Tracer : public TracerInterface {
                public:
                    /**
                     * Constructor.
                     *
                     * @param service_name The name of the service where the Tracer is running. This name is
                     * used in all annotations' endpoints of the spans created by the Tracer.
                     * @param address Pointer to a network-address object. The IP address and port are used
                     * in all annotations' endpoints of the spans created by the Tracer.
                     * @param random_generator Reference to the random-number generator to be used by the Tracer.
                     */
                    Tracer(const std::string &service_name, Runtime::RandomGenerator& random_generator) : service_name_(
                            service_name), reporter_(nullptr), random_generator_(random_generator){}

                    /**
                     * Creates a "root" XRay span.
                     *
                     * @param config The tracing configuration
                     * @param span_name Name of the new span.
                     * @param start_time The time indicating the beginning of the span.
                     */
                    SpanPtr startSpan(const Tracing::Config &, const std::string &span_name, SystemTime timestamp);

                    /**
                     * Depending on the given context, creates either a "child" or a "shared-context" XRay span.
                     *
                     * @param config The tracing configuration
                     * @param span_name Name of the new span.
                     * @param start_time The time indicating the beginning of the span.
                     * @param previous_context The context of the span preceding the one to be created.
                     */
                    SpanPtr startSpan(const Tracing::Config &, const std::string &span_name, SystemTime timestamp,
                                      SpanContext &previous_context);

                    /**
                     * TracerInterface::reportSpan.
                     */
                    void reportSpan(Span&& span) override;

                    /**
                     * Associates a Reporter object with this Tracer.
                     */
                    void setReporter(ReporterPtr reporter);

                    /**
                     * @return the service-name attribute associated with the Tracer.
                     */
                    const std::string &serviceName() const { return service_name_; }


                    /**
                     * @return the random-number generator associated with the Tracer.
                     */
                    Runtime::RandomGenerator &randomGenerator() { return random_generator_; }

                    std::string random_24bits_string();

                private:
                    static const std::string VERSION_;
                    static const std::string DELIMITER_;
                    const char *hex_digits = "0123456789abcdef";

                    const std::string service_name_;
                    ReporterPtr reporter_;
                    Runtime::RandomGenerator &random_generator_;
                };

                typedef std::unique_ptr<Tracer> TracerPtr;
            }
        }
    }
}