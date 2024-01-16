#pragma once

#include <string>

#include "envoy/local_info/local_info.h"
#include "envoy/tracing/tracer.h"

#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Tracing {

/**
 * Protocol independent tracer utility functions.
 */
class TracerUtility {
public:
  /**
   * Get string representation of the operation.
   * @param operation name to convert.
   * @return string representation of the operation.
   */
  static const std::string& toString(OperationName operation_name);

  /**
   * Request might be traceable if the request ID is traceable or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision shouldTraceRequest(const StreamInfo::StreamInfo& stream_info);

  /**
   * Finalize span and set protocol independent tags to the span.
   * @param span the downstream or upstream span.
   * @param context traceable stream context.
   * @param stream_info stream info.
   * @param config tracing configuration.
   * @param upstream_span true if the span is an upstream span.
   */
  static void finalizeSpan(Span& span, const TraceContext& context,
                           const StreamInfo::StreamInfo& stream_info, const Config& config,
                           bool upstream_span);

private:
  static const std::string IngressOperation;
  static const std::string EgressOperation;
};

class EgressConfigImpl : public Config {
public:
  // Tracing::Config
  Tracing::OperationName operationName() const override { return Tracing::OperationName::Egress; }
  const CustomTagMap* customTags() const override { return nullptr; }
  bool verbose() const override { return false; }
  uint32_t maxPathTagLength() const override { return Tracing::DefaultMaxPathTagLength; }
  // This EgressConfigImpl is only used for async client tracing. Return false here is OK.
  bool spawnUpstreamSpan() const override { return false; }
};

using EgressConfig = ConstSingleton<EgressConfigImpl>;

class NullTracer : public Tracer {
public:
  // Tracing::Tracer
  SpanPtr startSpan(const Config&, TraceContext&, const StreamInfo::StreamInfo&,
                    Tracing::Decision) override {
    return SpanPtr{new NullSpan()};
  }
};

class TracerImpl : public Tracer {
public:
  TracerImpl(DriverSharedPtr driver, const LocalInfo::LocalInfo& local_info);

  // Tracing::Tracer
  SpanPtr startSpan(const Config& config, TraceContext& trace_context,
                    const StreamInfo::StreamInfo& stream_info,
                    Tracing::Decision tracing_decision) override;

  DriverSharedPtr driverForTest() const { return driver_; }

private:
  DriverSharedPtr driver_;
  const LocalInfo::LocalInfo& local_info_;
};

} // namespace Tracing
} // namespace Envoy
