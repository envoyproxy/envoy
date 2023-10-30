#pragma once

#include "envoy/server/tracer_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/extensions/tracers/xray/tracer.h"
#include "source/extensions/tracers/xray/xray_configuration.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

class Driver : public Tracing::Driver, public Logger::Loggable<Logger::Id::tracing> {
public:
  Driver(const XRay::XRayConfiguration& config,
         Server::Configuration::TracerFactoryContext& context);

  // Tracing::Driver
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const StreamInfo::StreamInfo& stream_info,
                             const std::string& operation_name,
                             Tracing::Decision tracing_decision) override;

private:
  struct TlsTracer : ThreadLocal::ThreadLocalObject {
    TlsTracer(TracerPtr tracer, Driver& driver) : tracer_(std::move(tracer)), driver_(driver) {}
    TracerPtr tracer_;
    Driver& driver_;
  };

  XRayConfiguration xray_config_;
  SamplingStrategyPtr sampling_strategy_;
  ThreadLocal::SlotPtr tls_slot_ptr_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
