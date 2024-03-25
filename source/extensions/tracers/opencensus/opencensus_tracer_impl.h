#pragma once

#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/server/factory_context.h"
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
         Server::Configuration::CommonFactoryContext& context);

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
