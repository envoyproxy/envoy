#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/tracing/tracer.h"

#include "source/extensions/tracers/zipkin/span_context.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"
#include "source/extensions/tracers/zipkin/zipkin_core_types.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

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
   * @param shared_span_context Whether shared span id should be used.
   */
  Tracer(const std::string& service_name, Network::Address::InstanceConstSharedPtr address,
         Random::RandomGenerator& random_generator, const bool trace_id_128bit,
         const bool shared_span_context, TimeSource& time_source,
         bool split_spans_for_request = false)
      : service_name_(service_name), address_(address), reporter_(nullptr),
        random_generator_(random_generator), trace_id_128bit_(trace_id_128bit),
        shared_span_context_(shared_span_context), time_source_(time_source),
        split_spans_for_request_(split_spans_for_request) {}

  // TracerInterface
  SpanPtr startSpan(const Tracing::Config&, const std::string& span_name,
                    SystemTime timestamp) override;
  SpanPtr startSpan(const Tracing::Config&, const std::string& span_name, SystemTime timestamp,
                    const SpanContext& previous_context) override;
  void reportSpan(Span&& span) override;

  /**
   * Associates a Reporter object with this Tracer.
   *
   * @param The span reporter.
   */
  void setReporter(ReporterPtr reporter);

private:
  const std::string service_name_;
  Network::Address::InstanceConstSharedPtr address_;
  ReporterPtr reporter_;
  Random::RandomGenerator& random_generator_;
  const bool trace_id_128bit_;
  const bool shared_span_context_;
  TimeSource& time_source_;
  const bool split_spans_for_request_{};
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
