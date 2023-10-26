#pragma once

#include "envoy/api/api.h"
#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

/**
 * OpenCensus tracing driver.
 */
class Driver : public Tracing::Driver, Logger::Loggable<Logger::Id::tracing> {
public:
  Driver(const envoy::config::trace::v3::OpenCensusConfig& oc_config,
         const LocalInfo::LocalInfo& localinfo, Api::Api& api);

  // Tracing::Driver
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const StreamInfo::StreamInfo& stream_info,
                             const std::string& operation_name,
                             Tracing::Decision tracing_decision) override;

private:
  void applyTraceConfig(const opencensus::proto::trace::v1::TraceConfig& config);

  const envoy::config::trace::v3::OpenCensusConfig oc_config_;
  const LocalInfo::LocalInfo& local_info_;
};

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
