#pragma once

#include "envoy/server/instance.h"
#include "envoy/tracing/http_tracer.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/xray/tracer.h"
#include "extensions/tracers/xray/xray_configuration.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

class Driver : public Tracing::Driver, Logger::Loggable<Logger::Id::tracing> {
public:
  Driver(const XRay::XRayConfiguration& config, Server::Instance& server);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, Http::HeaderMap& request_headers,
                             const std::string& operation_name, SystemTime start_time,
                             const Tracing::Decision tracing_decision) override;

private:
  XRayConfiguration xray_config_;
  SamplingStrategyPtr sampling_strategy_;
  absl::optional<XRay::Tracer> tracer_; // optional<> for delayed construction
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
