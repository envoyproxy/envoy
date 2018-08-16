#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/runtime/runtime.h"
#include "envoy/tracing/http_tracer.h"

#include "extensions/tracers/zipkin/span_context.h"
#include "extensions/tracers/zipkin/tracer_interface.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"
#include "extensions/tracers/zipkin/zipkin_core_types.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

/**
 * Abstract class that delegates to users of the Tracer class the responsibility
 * of "reporting" a Zipkin span that has ended its life cycle. "Reporting" can mean that the
 * span will be sent to out to Zipkin, or buffered so that it can be sent out later.
 */
class Reporter {
public:
  /**
   * Destructor.
   */
  virtual ~Reporter() {}

  /**
   * Method that a concrete Reporter class must implement to handle finished spans.
   * For example, a span-buffer management policy could be implemented.
   *
   * @param span The span that needs action.
   */
  virtual void reportSpan(const Span& span) PURE;
};

typedef std::unique_ptr<Reporter> ReporterPtr;

/**
 * This class implements the Zipkin tracer. It has methods to create the appropriate Zipkin span
 * type, i.e., root span, child span, or shared-context span.
 *
 * This class allows its users to supply a concrete Reporter class whose reportSpan method
 * is called by its own reportSpan method. By doing so, we have cleanly separated the logic
 * of dealing with finished spans from the span-creation and tracing logic.
 */
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
   * @param trace_id_128bit Whether 128bit ids should be used.
   */
  Tracer(const std::string& service_name, Network::Address::InstanceConstSharedPtr address,
         Runtime::RandomGenerator& random_generator, const bool trace_id_128bit,
         TimeSource& time_source)
      : service_name_(service_name), address_(address), reporter_(nullptr),
        random_generator_(random_generator), trace_id_128bit_(trace_id_128bit),
        time_source_(time_source) {}

  /**
   * Creates a "root" Zipkin span.
   *
   * @param config The tracing configuration
   * @param span_name Name of the new span.
   * @param start_time The time indicating the beginning of the span.
   */
  SpanPtr startSpan(const Tracing::Config&, const std::string& span_name, SystemTime timestamp);

  /**
   * Depending on the given context, creates either a "child" or a "shared-context" Zipkin span.
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
   * @return the service-name attribute associated with the Tracer.
   */
  const std::string& serviceName() const { return service_name_; }

  /**
   * @return the pointer to the address object associated with the Tracer.
   */
  const Network::Address::InstanceConstSharedPtr address() const { return address_; }

  /**
   * Associates a Reporter object with this Tracer.
   */
  void setReporter(ReporterPtr reporter);

  /**
   * @return the random-number generator associated with the Tracer.
   */
  Runtime::RandomGenerator& randomGenerator() { return random_generator_; }

private:
  const std::string service_name_;
  Network::Address::InstanceConstSharedPtr address_;
  ReporterPtr reporter_;
  Runtime::RandomGenerator& random_generator_;
  const bool trace_id_128bit_;
  TimeSource& time_source_;
};

typedef std::unique_ptr<Tracer> TracerPtr;

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
