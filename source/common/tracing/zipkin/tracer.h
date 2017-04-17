#pragma once

#include "envoy/common/pure.h"
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

typedef std::shared_ptr<Reporter> ReporterSharedPtr;

typedef std::unique_ptr<Reporter> ReporterUniquePtr;

typedef std::shared_ptr<Runtime::RandomGenerator> RandomGeneratorSharedPtr;

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
   * @param address A string in the format <IP address>:<port>. The IP address and port are used
   * in all annotations' endpoints of the spans created by the Tracer.
   */
  Tracer(const std::string& service_name, const std::string& address)
      : service_name_(service_name), address_(address), random_generator_(nullptr) {}

  virtual ~Tracer() {}

  /**
   * Creates a "root" Zipkin span.
   *
   * @param span_name Name of the new span.
   * @param start_time The time indicating the beginning of the span.
   */
  Span startSpan(const std::string& span_name, uint64_t start_time);

  /**
   * Depending on the given context, creates either a "child" or a "shared-context" Zipkin span.
   *
   * @param span_name Name of the new span.
   * @param start_time The time indicating the beginning of the span.
   * @param previous_context The context of the span preceding the one to be created.
   */
  Span startSpan(const std::string& span_name, uint64_t start_time, SpanContext& previous_context);

  /**
   * TracerInterface::reportSpan.
   */
  void reportSpan(Span&& span) override;

  /**
   * @return the Reporter associated with the Tracer object.
   */
  ReporterSharedPtr reporter() { return reporter_; }

  /**
   * Associates a Reporter object with this Tracer.
   */
  void setReporter(ReporterUniquePtr reporter);

  /**
   * Provides a random-number generator to be used by the Tracer.
   * If this method is not used, the Tracer will use a default random-number generator.
   *
   * @param random_generator Random-number generator to be used.
   */
  void setRandomGenerator(RandomGeneratorSharedPtr random_generator);

private:
  /**
   * Uses the default random-number generator if one was not provided by the user
   */
  uint64_t generateRandomNumber();

  std::string service_name_;
  std::string address_;

  ReporterSharedPtr reporter_;

  RandomGeneratorSharedPtr random_generator_;
};
} // Zipkin
