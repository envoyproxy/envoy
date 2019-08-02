#include "extensions/tracers/opencensus/opencensus_tracer_impl.h"

#include "envoy/http/header_map.h"

#include "common/common/base64.h"

#include "absl/strings/str_cat.h"
#include "opencensus/exporters/trace/stackdriver/stackdriver_exporter.h"
#include "opencensus/exporters/trace/stdout/stdout_exporter.h"
#include "opencensus/exporters/trace/zipkin/zipkin_exporter.h"
#include "opencensus/trace/propagation/cloud_trace_context.h"
#include "opencensus/trace/propagation/grpc_trace_bin.h"
#include "opencensus/trace/propagation/trace_context.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/span_context.h"
#include "opencensus/trace/trace_config.h"
#include "opencensus/trace/trace_params.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

namespace {

class ConstantValues {
public:
  const Http::LowerCaseString TRACEPARENT{"traceparent"};
  const Http::LowerCaseString GRPC_TRACE_BIN{"grpc-trace-bin"};
  const Http::LowerCaseString X_CLOUD_TRACE_CONTEXT{"x-cloud-trace-context"};
};

using Constants = ConstSingleton<ConstantValues>;

/**
 * OpenCensus tracing implementation of the Envoy Span object.
 */
class Span : public Tracing::Span {
public:
  Span(const Tracing::Config& config, const envoy::config::trace::v2::OpenCensusConfig& oc_config,
       Http::HeaderMap& request_headers, const std::string& operation_name, SystemTime start_time,
       const Tracing::Decision tracing_decision);

  // Used by spawnChild().
  Span(const envoy::config::trace::v2::OpenCensusConfig& oc_config,
       ::opencensus::trace::Span&& span);

  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Http::HeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;

private:
  ::opencensus::trace::Span span_;
  const envoy::config::trace::v2::OpenCensusConfig& oc_config_;
};

::opencensus::trace::Span
startSpanHelper(const std::string& name, bool traced, const Http::HeaderMap& request_headers,
                const envoy::config::trace::v2::OpenCensusConfig& oc_config) {
  // Determine if there is a parent context.
  using OpenCensusConfig = envoy::config::trace::v2::OpenCensusConfig;
  ::opencensus::trace::SpanContext parent_ctx;
  for (const auto& incoming : oc_config.incoming_trace_context()) {
    bool found = false;
    switch (incoming) {
    case OpenCensusConfig::TRACE_CONTEXT: {
      const Http::HeaderEntry* header = request_headers.get(Constants::get().TRACEPARENT);
      if (header != nullptr) {
        found = true;
        parent_ctx = ::opencensus::trace::propagation::FromTraceParentHeader(
            header->value().getStringView());
      }
      break;
    }

    case OpenCensusConfig::GRPC_TRACE_BIN: {
      const Http::HeaderEntry* header = request_headers.get(Constants::get().GRPC_TRACE_BIN);
      if (header != nullptr) {
        found = true;
        parent_ctx = ::opencensus::trace::propagation::FromGrpcTraceBinHeader(
            Base64::decodeWithoutPadding(header->value().getStringView()));
      }
      break;
    }

    case OpenCensusConfig::CLOUD_TRACE_CONTEXT: {
      const Http::HeaderEntry* header = request_headers.get(Constants::get().X_CLOUD_TRACE_CONTEXT);
      if (header != nullptr) {
        found = true;
        parent_ctx = ::opencensus::trace::propagation::FromCloudTraceContextHeader(
            header->value().getStringView());
      }
      break;
    }
    }
    // First header found wins.
    if (found) {
      break;
    }
  }

  // Honor Envoy's tracing decision.
  ::opencensus::trace::AlwaysSampler always_sampler;
  ::opencensus::trace::NeverSampler never_sampler;
  // This is safe because opts are not used after StartSpan.
  ::opencensus::trace::StartSpanOptions opts{&never_sampler};
  if (traced) {
    opts.sampler = &always_sampler;
  }

  if (parent_ctx.IsValid()) {
    return ::opencensus::trace::Span::StartSpanWithRemoteParent(name, parent_ctx, opts);
  }
  return ::opencensus::trace::Span::StartSpan(name, /*parent=*/nullptr, opts);
}

Span::Span(const Tracing::Config& config,
           const envoy::config::trace::v2::OpenCensusConfig& oc_config,
           Http::HeaderMap& request_headers, const std::string& operation_name,
           SystemTime /*start_time*/, const Tracing::Decision tracing_decision)
    : span_(startSpanHelper(operation_name, tracing_decision.traced, request_headers, oc_config)),
      oc_config_(oc_config) {
  span_.AddAttribute("OperationName", config.operationName() == Tracing::OperationName::Ingress
                                          ? "Ingress"
                                          : "Egress");
}

Span::Span(const envoy::config::trace::v2::OpenCensusConfig& oc_config,
           ::opencensus::trace::Span&& span)
    : span_(std::move(span)), oc_config_(oc_config) {}

void Span::setOperation(absl::string_view operation) { span_.SetName(operation); }

void Span::setTag(absl::string_view name, absl::string_view value) {
  span_.AddAttribute(name, value);
}

void Span::log(SystemTime /*timestamp*/, const std::string& event) {
  // timestamp is ignored.
  span_.AddAnnotation(event);
}

void Span::finishSpan() { span_.End(); }

void Span::injectContext(Http::HeaderMap& request_headers) {
  using OpenCensusConfig = envoy::config::trace::v2::OpenCensusConfig;
  for (const auto& outgoing : oc_config_.outgoing_trace_context()) {
    switch (outgoing) {
    case OpenCensusConfig::TRACE_CONTEXT:
      request_headers.setReferenceKey(
          Constants::get().TRACEPARENT,
          ::opencensus::trace::propagation::ToTraceParentHeader(span_.context()));
      break;

    case OpenCensusConfig::GRPC_TRACE_BIN: {
      std::string val = ::opencensus::trace::propagation::ToGrpcTraceBinHeader(span_.context());
      val = Base64::encode(val.data(), val.size(), /*add_padding=*/false);
      request_headers.setReferenceKey(Constants::get().GRPC_TRACE_BIN, val);
      break;
    }

    case OpenCensusConfig::CLOUD_TRACE_CONTEXT:
      request_headers.setReferenceKey(
          Constants::get().X_CLOUD_TRACE_CONTEXT,
          ::opencensus::trace::propagation::ToCloudTraceContextHeader(span_.context()));
      break;
    }
  }
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config& /*config*/, const std::string& name,
                                  SystemTime /*start_time*/) {
  span_.AddAnnotation("spawnChild");
  return std::make_unique<Span>(oc_config_,
                                ::opencensus::trace::Span::StartSpan(name, /*parent=*/&span_));
}

void Span::setSampled(bool sampled) { span_.AddAnnotation("setSampled", {{"sampled", sampled}}); }

} // namespace

Driver::Driver(const envoy::config::trace::v2::OpenCensusConfig& oc_config,
               const LocalInfo::LocalInfo& localinfo)
    : oc_config_(oc_config), local_info_(localinfo) {
  if (oc_config.has_trace_config()) {
    applyTraceConfig(oc_config.trace_config());
  }
  if (oc_config.stdout_exporter_enabled()) {
    ::opencensus::exporters::trace::StdoutExporter::Register();
  }
  if (oc_config.stackdriver_exporter_enabled()) {
    ::opencensus::exporters::trace::StackdriverOptions opts;
    opts.project_id = oc_config.stackdriver_project_id();
    ::opencensus::exporters::trace::StackdriverExporter::Register(opts);
  }
  if (oc_config.zipkin_exporter_enabled()) {
    ::opencensus::exporters::trace::ZipkinExporterOptions opts(oc_config.zipkin_url());
    opts.service_name = local_info_.clusterName();
    ::opencensus::exporters::trace::ZipkinExporter::Register(opts);
  }
}

void Driver::applyTraceConfig(const opencensus::proto::trace::v1::TraceConfig& config) {
  using SamplerCase = opencensus::proto::trace::v1::TraceConfig::SamplerCase;
  using opencensus::proto::trace::v1::ConstantSampler;
  constexpr double kDefaultSamplingProbability = 1e-4;
  double probability = kDefaultSamplingProbability;

  switch (config.sampler_case()) {
  case SamplerCase::kProbabilitySampler:
    probability = config.probability_sampler().samplingprobability();
    break;
  case SamplerCase::kConstantSampler:
    switch (config.constant_sampler().decision()) {
    case ConstantSampler::ALWAYS_OFF:
      probability = 0.;
      break;
    case ConstantSampler::ALWAYS_ON:
    case ConstantSampler::ALWAYS_PARENT:
      probability = 1.;
      break;
    default:
      break; /* Keep default probability. */
    }
    break;
  case SamplerCase::kRateLimitingSampler:
    ENVOY_LOG(error, "RateLimitingSampler is not supported.");
    break;
  case SamplerCase::SAMPLER_NOT_SET:
    break; // Keep default.
  default:
    ENVOY_LOG(error, "Unknown sampler type in TraceConfig.");
  }

  ::opencensus::trace::TraceConfig::SetCurrentTraceParams(::opencensus::trace::TraceParams{
      uint32_t(config.max_number_of_attributes()), uint32_t(config.max_number_of_annotations()),
      uint32_t(config.max_number_of_message_events()), uint32_t(config.max_number_of_links()),
      ::opencensus::trace::ProbabilitySampler(probability)});
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config, Http::HeaderMap& request_headers,
                                   const std::string& operation_name, SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  return std::make_unique<Span>(config, oc_config_, request_headers, operation_name, start_time,
                                tracing_decision);
}

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
