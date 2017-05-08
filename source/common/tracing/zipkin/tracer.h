#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/runtime/runtime.h"

#include "common/tracing/zipkin/span_context.h"
#include "common/tracing/zipkin/tracer_interface.h"
#include "common/tracing/zipkin/zipkin_core_types.h"

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
  virtual void reportSpan(Span&& span) PURE;
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
   * Move constructor
   */
  Tracer(Tracer&& tracer)
      : service_name_(tracer.serviceName()), address_(tracer.address()),
        reporter_(tracer.reporter()), random_generator_(tracer.randomGenerator()) {}

  /**
   * Constructor.
   *
   * @param service_name The name of the service where the Tracer is running. This name is
   * used in all annotations' endpoints of the spans created by the Tracer.
   * @param address Pointer to a network-address object. The IP address and port are used
   * in all annotations' endpoints of the spans created by the Tracer.
   */
  Tracer(const std::string& service_name, Network::Address::InstanceConstSharedPtr address)
      : service_name_(service_name), address_(address), reporter_(nullptr),
        random_generator_(nullptr) {}

  /**
   * Creates a "root" Zipkin span.
   *
   * @param span_name Name of the new span.
   * @param start_time The time indicating the beginning of the span.
   */
  SpanPtr startSpan(const std::string& span_name, SystemTime timestamp);

  /**
   * Depending on the given context, creates either a "child" or a "shared-context" Zipkin span.
   *
   * @param span_name Name of the new span.
   * @param start_time The time indicating the beginning of the span.
   * @param previous_context The context of the span preceding the one to be created.
   */
  SpanPtr startSpan(const std::string& span_name, SystemTime timestamp,
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
   * @return the reporter associated with the Tracer.
   * This is only implemented to support the move constructor.
   */
  ReporterPtr reporter() { return std::move(reporter_); }

  /**
   * Provides a random-number generator to be used by the Tracer.
   * If this method is not used, the Tracer will use a default random-number generator.
   *
   * @param random_generator Random-number generator to be used.
   */
  void setRandomGenerator(Runtime::RandomGeneratorPtr random_generator);

  /**
   *  @return the random generator associated with the Tracer
   *  This is only implemented to support the move constructor.
   */
  Runtime::RandomGeneratorPtr randomGenerator() { return std::move(random_generator_); }

private:
  /**
   * Uses the default random-number generator if one was not provided by the user
   */
  uint64_t generateRandomNumber();

  const std::string service_name_;
  Network::Address::InstanceConstSharedPtr address_;
  ReporterPtr reporter_;
  Runtime::RandomGeneratorPtr random_generator_;
};

typedef std::unique_ptr<Tracer> TracerPtr;

} // Zipkin
