#pragma once

#include "envoy/config/trace/v2/trace.pb.validate.h"
#include "envoy/tracing/http_tracer.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

/**
 * OpenCensus tracing driver.
 */
class Driver : public Tracing::Driver, Logger::Loggable<Logger::Id::tracing> {
public:
  Driver(const envoy::config::trace::v2::OpenCensusConfig& oc_config);

  /**
   * Implements the abstract Driver's startSpan operation.
   */
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Http::HeaderMap& request_headers,
                             const std::string& operation_name, SystemTime start_time,
                             const Tracing::Decision tracing_decision) override;

private:
  void applyTraceConfig(const opencensus::proto::trace::v1::TraceConfig& config);

  const envoy::config::trace::v2::OpenCensusConfig oc_config_;
};

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
