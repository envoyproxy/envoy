#pragma once

#include "envoy/server/tracer_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/xray/tracer.h"
#include "extensions/tracers/xray/xray_configuration.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

class Driver : public Tracing::Driver, public Logger::Loggable<Logger::Id::tracing> {
public:
  Driver(const XRay::XRayConfiguration& config,
         Server::Configuration::TracerFactoryContext& context);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, Http::RequestHeaderMap& request_headers,
                             const std::string& operation_name, Envoy::SystemTime start_time,
                             const Tracing::Decision tracing_decision) override;

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
