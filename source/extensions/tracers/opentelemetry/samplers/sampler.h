#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/tracer_config.h"
#include "envoy/tracing/trace_context.h"

#include "source/extensions/tracers/opentelemetry/otlp_utils.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class SpanContext;

enum class Decision {
  // IsRecording will be false, the Span will not be recorded and all events and attributes will be
  // dropped.
  Drop,
  // IsRecording will be true, but the Sampled flag MUST NOT be set.
  RecordOnly,
  // IsRecording will be true and the Sampled flag MUST be set.
  RecordAndSample
};

struct SamplingResult {
  /// @see Decision
  Decision decision;
  // A set of span Attributes that will also be added to the Span. Can be nullptr.
  std::unique_ptr<const OtelAttributes> attributes;
  // A Tracestate that will be associated with the Span. If the sampler
  // returns an empty Tracestate here, the Tracestate will be cleared, so samplers SHOULD normally
  // return the passed-in Tracestate if they do not intend to change it
  std::string tracestate;

  inline bool isRecording() const {
    return decision == Decision::RecordOnly || decision == Decision::RecordAndSample;
  }

  inline bool isSampled() const { return decision == Decision::RecordAndSample; }
};

/**
 * @brief The base type for all samplers
 * see https://opentelemetry.io/docs/specs/otel/trace/sdk/#sampler
 *
 */
class Sampler {
public:
  virtual ~Sampler() = default;

  /**
   * @brief Decides if a trace should be sampled.
   *
   * @param parent_context Span context describing the parent span. The Span's SpanContext may be
   * invalid to indicate a root span.
   * @param trace_id Trace id of the Span to be created. If the parent SpanContext contains a valid
   * TraceId, they MUST always match.
   * @param name Name of the Span to be created.
   * @param spankind Span kind of the Span to be created.
   * @param trace_context TraceContext containing potential initial span attributes
   * @param links Collection of links that will be associated with the Span to be created.
   * @return SamplingResult @see SamplingResult
   */
  virtual SamplingResult shouldSample(const absl::optional<SpanContext> parent_context,
                                      const std::string& trace_id, const std::string& name,
                                      OTelSpanKind spankind,
                                      OptRef<const Tracing::TraceContext> trace_context,
                                      const std::vector<SpanContext>& links) PURE;

  /**
   * @brief Returns a sampler description or name.
   *
   * @return The sampler name or short description with the configuration.
   */
  virtual std::string getDescription() const PURE;
};

using SamplerSharedPtr = std::shared_ptr<Sampler>;

/*
 * A factory for creating a sampler
 */
class SamplerFactory : public Envoy::Config::TypedFactory {
public:
  ~SamplerFactory() override = default;

  /**
   * @brief Creates a sampler
   * @param config The sampler protobuf config.
   * @param context The TracerFactoryContext.
   * @return SamplerSharedPtr A sampler.
   */
  virtual SamplerSharedPtr createSampler(const Protobuf::Message& config,
                                         Server::Configuration::TracerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers.opentelemetry.samplers"; }
};

using SamplerFactoryPtr = std::unique_ptr<SamplerFactory>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
