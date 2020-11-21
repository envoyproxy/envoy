#include "extensions/tracers/opencensus/opencensus_tracer_impl.h"

#include <grpcpp/grpcpp.h>

#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/http/header_map.h"

#include "common/common/base64.h"

#include "absl/strings/str_cat.h"
#include "google/devtools/cloudtrace/v2/tracing.grpc.pb.h"
#include "opencensus/exporters/trace/ocagent/ocagent_exporter.h"
#include "opencensus/exporters/trace/stackdriver/stackdriver_exporter.h"
#include "opencensus/exporters/trace/stdout/stdout_exporter.h"
#include "opencensus/exporters/trace/zipkin/zipkin_exporter.h"
#include "opencensus/trace/propagation/b3.h"
#include "opencensus/trace/propagation/cloud_trace_context.h"
#include "opencensus/trace/propagation/grpc_trace_bin.h"
#include "opencensus/trace/propagation/trace_context.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/span_context.h"
#include "opencensus/trace/trace_config.h"
#include "opencensus/trace/trace_params.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "common/grpc/google_grpc_utils.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

#ifdef ENVOY_GOOGLE_GRPC
constexpr char GoogleStackdriverTraceAddress[] = "cloudtrace.googleapis.com";
#endif

namespace {

class ConstantValues {
public:
  const Http::LowerCaseString TRACEPARENT{"traceparent"};
  const Http::LowerCaseString GRPC_TRACE_BIN{"grpc-trace-bin"};
  const Http::LowerCaseString X_CLOUD_TRACE_CONTEXT{"x-cloud-trace-context"};
  const Http::LowerCaseString X_B3_TRACEID{"x-b3-traceid"};
  const Http::LowerCaseString X_B3_SPANID{"x-b3-spanid"};
  const Http::LowerCaseString X_B3_SAMPLED{"x-b3-sampled"};
  const Http::LowerCaseString X_B3_FLAGS{"x-b3-flags"};
};

using Constants = ConstSingleton<ConstantValues>;

/**
 * OpenCensus tracing implementation of the Envoy Span object.
 */
class Span : public Tracing::Span {
public:
  Span(const Tracing::Config& config, const envoy::config::trace::v3::OpenCensusConfig& oc_config,
       Http::RequestHeaderMap& request_headers, const std::string& operation_name,
       SystemTime start_time, const Tracing::Decision tracing_decision);

  // Used by spawnChild().
  Span(const envoy::config::trace::v3::OpenCensusConfig& oc_config,
       ::opencensus::trace::Span&& span);

  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Http::RequestHeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;

  // OpenCensus doesn't support baggage, so noop these OpenTracing functions.
  void setBaggage(absl::string_view, absl::string_view) override{};
  std::string getBaggage(absl::string_view) override { return std::string(); };

private:
  ::opencensus::trace::Span span_;
  const envoy::config::trace::v3::OpenCensusConfig& oc_config_;
};

::opencensus::trace::Span
startSpanHelper(const std::string& name, bool traced, const Http::RequestHeaderMap& request_headers,
                const envoy::config::trace::v3::OpenCensusConfig& oc_config) {
  // Determine if there is a parent context.
  using OpenCensusConfig = envoy::config::trace::v3::OpenCensusConfig;
  ::opencensus::trace::SpanContext parent_ctx;
  for (const auto& incoming : oc_config.incoming_trace_context()) {
    bool found = false;
    switch (incoming) {
    case OpenCensusConfig::TRACE_CONTEXT: {
      const auto header = request_headers.get(Constants::get().TRACEPARENT);
      if (!header.empty()) {
        found = true;
        // This is an implicitly untrusted header, so only the first value is used.
        parent_ctx = ::opencensus::trace::propagation::FromTraceParentHeader(
            header[0]->value().getStringView());
      }
      break;
    }

    case OpenCensusConfig::GRPC_TRACE_BIN: {
      const auto header = request_headers.get(Constants::get().GRPC_TRACE_BIN);
      if (!header.empty()) {
        found = true;
        // This is an implicitly untrusted header, so only the first value is used.
        parent_ctx = ::opencensus::trace::propagation::FromGrpcTraceBinHeader(
            Base64::decodeWithoutPadding(header[0]->value().getStringView()));
      }
      break;
    }

    case OpenCensusConfig::CLOUD_TRACE_CONTEXT: {
      const auto header = request_headers.get(Constants::get().X_CLOUD_TRACE_CONTEXT);
      if (!header.empty()) {
        found = true;
        // This is an implicitly untrusted header, so only the first value is used.
        parent_ctx = ::opencensus::trace::propagation::FromCloudTraceContextHeader(
            header[0]->value().getStringView());
      }
      break;
    }

    case OpenCensusConfig::B3: {
      absl::string_view b3_trace_id;
      absl::string_view b3_span_id;
      absl::string_view b3_sampled;
      absl::string_view b3_flags;
      const auto h_b3_trace_id = request_headers.get(Constants::get().X_B3_TRACEID);
      if (!h_b3_trace_id.empty()) {
        // This is an implicitly untrusted header, so only the first value is used.
        b3_trace_id = h_b3_trace_id[0]->value().getStringView();
      }
      const auto h_b3_span_id = request_headers.get(Constants::get().X_B3_SPANID);
      if (!h_b3_span_id.empty()) {
        // This is an implicitly untrusted header, so only the first value is used.
        b3_span_id = h_b3_span_id[0]->value().getStringView();
      }
      const auto h_b3_sampled = request_headers.get(Constants::get().X_B3_SAMPLED);
      if (!h_b3_sampled.empty()) {
        // This is an implicitly untrusted header, so only the first value is used.
        b3_sampled = h_b3_sampled[0]->value().getStringView();
      }
      const auto h_b3_flags = request_headers.get(Constants::get().X_B3_FLAGS);
      if (!h_b3_flags.empty()) {
        // This is an implicitly untrusted header, so only the first value is used.
        b3_flags = h_b3_flags[0]->value().getStringView();
      }
      if (!h_b3_trace_id.empty() && !h_b3_span_id.empty()) {
        found = true;
        parent_ctx = ::opencensus::trace::propagation::FromB3Headers(b3_trace_id, b3_span_id,
                                                                     b3_sampled, b3_flags);
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
           const envoy::config::trace::v3::OpenCensusConfig& oc_config,
           Http::RequestHeaderMap& request_headers, const std::string& operation_name,
           SystemTime /*start_time*/, const Tracing::Decision tracing_decision)
    : span_(startSpanHelper(operation_name, tracing_decision.traced, request_headers, oc_config)),
      oc_config_(oc_config) {
  span_.AddAttribute("OperationName", config.operationName() == Tracing::OperationName::Ingress
                                          ? "Ingress"
                                          : "Egress");
}

Span::Span(const envoy::config::trace::v3::OpenCensusConfig& oc_config,
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

void Span::injectContext(Http::RequestHeaderMap& request_headers) {
  using OpenCensusConfig = envoy::config::trace::v3::OpenCensusConfig;
  const auto& ctx = span_.context();
  for (const auto& outgoing : oc_config_.outgoing_trace_context()) {
    switch (outgoing) {
    case OpenCensusConfig::TRACE_CONTEXT:
      request_headers.setReferenceKey(Constants::get().TRACEPARENT,
                                      ::opencensus::trace::propagation::ToTraceParentHeader(ctx));
      break;

    case OpenCensusConfig::GRPC_TRACE_BIN: {
      std::string val = ::opencensus::trace::propagation::ToGrpcTraceBinHeader(ctx);
      val = Base64::encode(val.data(), val.size(), /*add_padding=*/false);
      request_headers.setReferenceKey(Constants::get().GRPC_TRACE_BIN, val);
      break;
    }

    case OpenCensusConfig::CLOUD_TRACE_CONTEXT:
      request_headers.setReferenceKey(
          Constants::get().X_CLOUD_TRACE_CONTEXT,
          ::opencensus::trace::propagation::ToCloudTraceContextHeader(ctx));
      break;

    case OpenCensusConfig::B3:
      request_headers.setReferenceKey(Constants::get().X_B3_TRACEID,
                                      ::opencensus::trace::propagation::ToB3TraceIdHeader(ctx));
      request_headers.setReferenceKey(Constants::get().X_B3_SPANID,
                                      ::opencensus::trace::propagation::ToB3SpanIdHeader(ctx));
      request_headers.setReferenceKey(Constants::get().X_B3_SAMPLED,
                                      ::opencensus::trace::propagation::ToB3SampledHeader(ctx));
      // OpenCensus's trace context propagation doesn't produce the
      // "X-B3-Flags:" header.
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

Driver::Driver(const envoy::config::trace::v3::OpenCensusConfig& oc_config,
               const LocalInfo::LocalInfo& localinfo, Api::Api& api)
    : oc_config_(oc_config), local_info_(localinfo) {
  // To give user a chance to correct initially invalid configuration and try to apply it once again
  // without a need to restart Envoy, validation checks must be done prior to any side effects.
  if (oc_config.stackdriver_exporter_enabled() && oc_config.has_stackdriver_grpc_service() &&
      !oc_config.stackdriver_grpc_service().has_google_grpc()) {
    throw EnvoyException("Opencensus stackdriver tracer only support GoogleGrpc.");
  }
  if (oc_config.ocagent_exporter_enabled() && oc_config.has_ocagent_grpc_service() &&
      !oc_config.ocagent_grpc_service().has_google_grpc()) {
    throw EnvoyException("Opencensus ocagent tracer only supports GoogleGrpc.");
  }
  // Process-wide side effects.
  if (oc_config.has_trace_config()) {
    applyTraceConfig(oc_config.trace_config());
  }
  if (oc_config.stdout_exporter_enabled()) {
    ::opencensus::exporters::trace::StdoutExporter::Register();
  }
  if (oc_config.stackdriver_exporter_enabled()) {
    ::opencensus::exporters::trace::StackdriverOptions opts;
    opts.project_id = oc_config.stackdriver_project_id();
    if (!oc_config.stackdriver_address().empty()) {
      auto channel =
          grpc::CreateChannel(oc_config.stackdriver_address(), grpc::InsecureChannelCredentials());
      opts.trace_service_stub = ::google::devtools::cloudtrace::v2::TraceService::NewStub(channel);
    } else if (oc_config.has_stackdriver_grpc_service() &&
               oc_config.stackdriver_grpc_service().has_google_grpc()) {
#ifdef ENVOY_GOOGLE_GRPC
      envoy::config::core::v3::GrpcService stackdriver_service =
          oc_config.stackdriver_grpc_service();
      if (stackdriver_service.google_grpc().target_uri().empty()) {
        // If stackdriver server address is not provided, the default production stackdriver
        // address will be used.
        stackdriver_service.mutable_google_grpc()->set_target_uri(GoogleStackdriverTraceAddress);
      }
      auto channel = Envoy::Grpc::GoogleGrpcUtils::createChannel(stackdriver_service, api);
      // TODO(bianpengyuan): add tests for trace_service_stub and initial_metadata options with mock
      // stubs.
      opts.trace_service_stub = ::google::devtools::cloudtrace::v2::TraceService::NewStub(channel);
      const auto& initial_metadata = stackdriver_service.initial_metadata();
      if (!initial_metadata.empty()) {
        opts.prepare_client_context = [initial_metadata](grpc::ClientContext* ctx) {
          for (const auto& metadata : initial_metadata) {
            ctx->AddMetadata(metadata.key(), metadata.value());
          }
        };
      }
#else
      throw EnvoyException("Opencensus tracer: cannot handle stackdriver google grpc service, "
                           "google grpc is not built in.");
#endif
    }
    ::opencensus::exporters::trace::StackdriverExporter::Register(std::move(opts));
  }
  if (oc_config.zipkin_exporter_enabled()) {
    ::opencensus::exporters::trace::ZipkinExporterOptions opts(oc_config.zipkin_url());
    opts.service_name = local_info_.clusterName();
    ::opencensus::exporters::trace::ZipkinExporter::Register(opts);
  }
  if (oc_config.ocagent_exporter_enabled()) {
    ::opencensus::exporters::trace::OcAgentOptions opts;
    if (!oc_config.ocagent_address().empty()) {
      opts.address = oc_config.ocagent_address();
    } else if (oc_config.has_ocagent_grpc_service() &&
               oc_config.ocagent_grpc_service().has_google_grpc()) {
#ifdef ENVOY_GOOGLE_GRPC
      const envoy::config::core::v3::GrpcService& ocagent_service =
          oc_config.ocagent_grpc_service();
      auto channel = Envoy::Grpc::GoogleGrpcUtils::createChannel(ocagent_service, api);
      opts.trace_service_stub =
          ::opencensus::proto::agent::trace::v1::TraceService::NewStub(channel);
#else
      throw EnvoyException("Opencensus tracer: cannot handle ocagent google grpc service, google "
                           "grpc is not built in.");
#endif
    }
    opts.service_name = local_info_.clusterName();
    ::opencensus::exporters::trace::OcAgentExporter::Register(std::move(opts));
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

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Http::RequestHeaderMap& request_headers,
                                   const std::string& operation_name, SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  return std::make_unique<Span>(config, oc_config_, request_headers, operation_name, start_time,
                                tracing_decision);
}

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
