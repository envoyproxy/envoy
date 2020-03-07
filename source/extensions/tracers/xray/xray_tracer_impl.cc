#include "extensions/tracers/xray/xray_tracer_impl.h"

#include "common/common/macros.h"
#include "common/common/utility.h"

#include "extensions/tracers/xray/localized_sampling.h"
#include "extensions/tracers/xray/tracer.h"
#include "extensions/tracers/xray/xray_configuration.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {
constexpr auto DefaultDaemonEndpoint = "127.0.0.1:2000";
XRayHeader parseXRayHeader(const Http::LowerCaseString& header) {
  const auto& lowered_header = header.get();
  XRayHeader result;
  for (const auto& token : StringUtil::splitToken(lowered_header, ";")) {
    if (absl::StartsWith(token, "root=")) {
      result.trace_id_ = std::string(StringUtil::cropLeft(token, "="));
    } else if (absl::StartsWith(token, "parent=")) {
      result.parent_id_ = std::string(StringUtil::cropLeft(token, "="));
    } else if (absl::StartsWith(token, "sampled=")) {
      const auto s = StringUtil::cropLeft(token, "=");
      if (s == "1") {
        result.sample_decision_ = SamplingDecision::Sampled;
      } else if (s == "0") {
        result.sample_decision_ = SamplingDecision::NotSampled;
      } else {
        result.sample_decision_ = SamplingDecision::Unknown;
      }
    }
  }
  return result;
}
} // namespace

Driver::Driver(const XRayConfiguration& config,
               Server::Configuration::TracerFactoryContext& context)
    : xray_config_(config),
      tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {

  const std::string daemon_endpoint =
      config.daemon_endpoint_.empty() ? DefaultDaemonEndpoint : config.daemon_endpoint_;

  ENVOY_LOG(debug, "send X-Ray generated segments to daemon address on {}", daemon_endpoint);
  sampling_strategy_ = std::make_unique<XRay::LocalizedSamplingStrategy>(
      xray_config_.sampling_rules_, context.serverFactoryContext().random(),
      context.serverFactoryContext().timeSource());

  tls_slot_ptr_->set([this, daemon_endpoint,
                      &context](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    std::string span_name = xray_config_.segment_name_.empty()
                                ? context.serverFactoryContext().localInfo().clusterName()
                                : xray_config_.segment_name_;

    DaemonBrokerPtr broker = std::make_unique<DaemonBrokerImpl>(daemon_endpoint);
    TracerPtr tracer = std::make_unique<Tracer>(span_name, std::move(broker),
                                                context.serverFactoryContext().timeSource());
    return std::make_shared<XRay::Driver::TlsTracer>(std::move(tracer), *this);
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Http::RequestHeaderMap& request_headers,
                                   const std::string& operation_name, Envoy::SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  // First thing is to determine whether this request will be sampled or not.
  // if there's a X-Ray header and it has a sampling decision already determined (i.e. Sample=1)
  // then we can move on; otherwise, we ask the sampling strategy whether this request should be
  // sampled or not.
  //
  // The second step is create a Span.
  // If we have a XRay TraceID in the headers, then we create a SpanContext to pass that trace-id
  // around if no TraceID (which means no x-ray header) then this is a brand new span.

  UNREFERENCED_PARAMETER(config);
  // TODO(marcomagdy) - how do we factor this into the logic above
  UNREFERENCED_PARAMETER(tracing_decision);
  const auto* header = request_headers.get(Http::LowerCaseString(XRayTraceHeader));
  absl::optional<bool> should_trace;
  XRayHeader xray_header;
  if (header) {
    Http::LowerCaseString lowered_header_value{std::string(header->value().getStringView())};
    xray_header = parseXRayHeader(lowered_header_value);
    // if the sample_decision in the x-ray header is unknown then we try to make a decision based
    // on the sampling strategy
    if (xray_header.sample_decision_ == SamplingDecision::Sampled) {
      should_trace = true;
    } else if (xray_header.sample_decision_ == SamplingDecision::NotSampled) {
      should_trace = false;
    } else {
      ENVOY_LOG(
          trace,
          "Unable to determine from the X-Ray trace header whether request is sampled or not");
    }
  }

  if (!should_trace.has_value()) {
    const SamplingRequest request{std::string{request_headers.Host()->value().getStringView()},
                                  std::string{request_headers.Method()->value().getStringView()},
                                  std::string{request_headers.Path()->value().getStringView()}};

    should_trace = sampling_strategy_->shouldTrace(request);
  }

  auto* tracer = tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer_.get();
  if (should_trace.value()) {
    return tracer->startSpan(operation_name, start_time,
                             header ? absl::optional<XRayHeader>(xray_header) : absl::nullopt);
  }

  // instead of returning nullptr, we return a Span that is marked as not-sampled.
  // This is important to communicate that information to upstream services (see injectContext()).
  // Otherwise, the upstream service can decide to sample the request regardless and we end up with
  // more samples than we asked for.
  return tracer->createNonSampledSpan();
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
