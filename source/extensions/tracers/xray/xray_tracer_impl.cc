#include "extensions/tracers/xray/xray_tracer_impl.h"

#include "common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

static const char DefaultDaemonEndpoint[] = "127.0.0.1:2000";
Driver::Driver(const XRayConfiguration& config, Server::Instance& server) : xray_config_(config) {

  const std::string daemon_endpoint =
      config.daemon_endpoint_.empty() ? DefaultDaemonEndpoint : config.daemon_endpoint_;

  ENVOY_LOG(debug, "send X-Ray generated segments to daemon address on {}", daemon_endpoint);
  std::string span_name =
      config.segment_name_.empty() ? server.localInfo().clusterName() : config.segment_name_;

  sampling_strategy_ = std::make_unique<XRay::SamplingStrategy>(server.random().random());
  tracer_.emplace(span_name, server.timeSource());
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config, Http::HeaderMap& request_headers,
                                   const std::string& operation_name, SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {

  UNREFERENCED_PARAMETER(config);
  UNREFERENCED_PARAMETER(operation_name);
  UNREFERENCED_PARAMETER(start_time);
  UNREFERENCED_PARAMETER(tracing_decision);
  const SamplingRequest request{request_headers.Host()->value().getStringView(),
                                request_headers.Method()->value().getStringView(),
                                request_headers.Path()->value().getStringView()};

  if (!sampling_strategy_->sampleRequest(request)) {
    return nullptr;
  }

  return tracer_->startSpan();
}
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
