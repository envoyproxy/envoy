#include "source/extensions/tracers/dynamic_modules/tracer_config.h"

#include "source/common/common/assert.h"
#include "source/common/tracing/null_span_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicModules {

namespace {

envoy_dynamic_module_type_trace_reason toAbiReason(Tracing::Reason reason) {
  switch (reason) {
  case Tracing::Reason::NotTraceable:
    return envoy_dynamic_module_type_trace_reason_NotTraceable;
  case Tracing::Reason::HealthCheck:
    return envoy_dynamic_module_type_trace_reason_HealthCheck;
  case Tracing::Reason::Sampling:
    return envoy_dynamic_module_type_trace_reason_Sampling;
  case Tracing::Reason::ServiceForced:
    return envoy_dynamic_module_type_trace_reason_ServiceForced;
  case Tracing::Reason::ClientForced:
    return envoy_dynamic_module_type_trace_reason_ClientForced;
  }
  return envoy_dynamic_module_type_trace_reason_NotTraceable;
}

} // namespace

// =============================================================================
// DynamicModuleTracerConfig
// =============================================================================

DynamicModuleTracerConfig::DynamicModuleTracerConfig(
    const absl::string_view tracer_name, const absl::string_view tracer_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope)
    : stats_scope_(stats_scope.createScope(absl::StrCat(metrics_namespace, "."))),
      stat_name_pool_(stats_scope_->symbolTable()), tracer_name_(tracer_name),
      tracer_config_(tracer_config), dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleTracerConfig::~DynamicModuleTracerConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleTracerConfigSharedPtr> newDynamicModuleTracerConfig(
    const absl::string_view tracer_name, const absl::string_view tracer_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto config = std::make_shared<DynamicModuleTracerConfig>(
      tracer_name, tracer_config, metrics_namespace, std::move(dynamic_module), stats_scope);

#define RESOLVE_OR_RETURN(field, symbol)                                                           \
  {                                                                                                \
    auto result = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);    \
    RETURN_IF_NOT_OK_REF(result.status());                                                         \
    config->field = result.value();                                                                \
  }

  // Resolve required config symbols.
  auto on_config_new = config->dynamic_module_->getFunctionPointer<OnTracerConfigNewType>(
      "envoy_dynamic_module_on_tracer_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  RESOLVE_OR_RETURN(on_config_destroy_, "envoy_dynamic_module_on_tracer_config_destroy");

  // Resolve required span symbols.
  RESOLVE_OR_RETURN(on_start_span_, "envoy_dynamic_module_on_tracer_start_span");
  RESOLVE_OR_RETURN(on_span_set_operation_, "envoy_dynamic_module_on_tracer_span_set_operation");
  RESOLVE_OR_RETURN(on_span_set_tag_, "envoy_dynamic_module_on_tracer_span_set_tag");
  RESOLVE_OR_RETURN(on_span_log_, "envoy_dynamic_module_on_tracer_span_log");
  RESOLVE_OR_RETURN(on_span_finish_, "envoy_dynamic_module_on_tracer_span_finish");
  RESOLVE_OR_RETURN(on_span_inject_context_, "envoy_dynamic_module_on_tracer_span_inject_context");
  RESOLVE_OR_RETURN(on_span_spawn_child_, "envoy_dynamic_module_on_tracer_span_spawn_child");
  RESOLVE_OR_RETURN(on_span_set_sampled_, "envoy_dynamic_module_on_tracer_span_set_sampled");
  RESOLVE_OR_RETURN(on_span_use_local_decision_,
                    "envoy_dynamic_module_on_tracer_span_use_local_decision");
  RESOLVE_OR_RETURN(on_span_get_baggage_, "envoy_dynamic_module_on_tracer_span_get_baggage");
  RESOLVE_OR_RETURN(on_span_set_baggage_, "envoy_dynamic_module_on_tracer_span_set_baggage");
  RESOLVE_OR_RETURN(on_span_get_trace_id_, "envoy_dynamic_module_on_tracer_span_get_trace_id");
  RESOLVE_OR_RETURN(on_span_get_span_id_, "envoy_dynamic_module_on_tracer_span_get_span_id");
  RESOLVE_OR_RETURN(on_span_destroy_, "envoy_dynamic_module_on_tracer_span_destroy");

#undef RESOLVE_OR_RETURN

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->tracer_name_.data(),
                                                     .length = config->tracer_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->tracer_config_.data(),
                                                       .length = config->tracer_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module tracer config");
  }
  config->stat_creation_frozen_ = true;
  return config;
}

// =============================================================================
// DynamicModuleSpan
// =============================================================================

DynamicModuleSpan::DynamicModuleSpan(
    DynamicModuleTracerConfigSharedPtr config,
    envoy_dynamic_module_type_tracer_span_module_ptr in_module_span,
    Tracing::TraceContext* trace_context)
    : config_(std::move(config)), in_module_span_(in_module_span), trace_context_(trace_context) {}

DynamicModuleSpan::~DynamicModuleSpan() {
  if (in_module_span_ != nullptr && config_->on_span_destroy_ != nullptr) {
    config_->on_span_destroy_(in_module_span_);
  }
}

void DynamicModuleSpan::setOperation(absl::string_view operation) {
  envoy_dynamic_module_type_envoy_buffer op_buf = {.ptr = const_cast<char*>(operation.data()),
                                                   .length = operation.size()};
  config_->on_span_set_operation_(in_module_span_, op_buf);
}

void DynamicModuleSpan::setTag(absl::string_view name, absl::string_view value) {
  envoy_dynamic_module_type_envoy_buffer key_buf = {.ptr = const_cast<char*>(name.data()),
                                                    .length = name.size()};
  envoy_dynamic_module_type_envoy_buffer val_buf = {.ptr = const_cast<char*>(value.data()),
                                                    .length = value.size()};
  config_->on_span_set_tag_(in_module_span_, key_buf, val_buf);
}

void DynamicModuleSpan::log(SystemTime timestamp, const std::string& event) {
  const int64_t timestamp_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp.time_since_epoch()).count();
  envoy_dynamic_module_type_envoy_buffer event_buf = {.ptr = const_cast<char*>(event.data()),
                                                      .length = event.size()};
  config_->on_span_log_(in_module_span_, timestamp_ns, event_buf);
}

bool DynamicModuleSpan::exportedSpan() const {
  // TODO(jkoch): extend module ABI with hook as an optimization
  return true;
}

void DynamicModuleSpan::finishSpan() { config_->on_span_finish_(in_module_span_); }

void DynamicModuleSpan::injectContext(Tracing::TraceContext& trace_context,
                                      const Tracing::UpstreamContext&) {
  // Temporarily set the trace context to the outgoing one for callback access.
  Tracing::TraceContext* prev = trace_context_;
  trace_context_ = &trace_context;
  config_->on_span_inject_context_(in_module_span_, static_cast<void*>(this));
  trace_context_ = prev;
}

Tracing::SpanPtr DynamicModuleSpan::spawnChild(const Tracing::Config&, const std::string& name,
                                               SystemTime start_time) {
  const int64_t start_time_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(start_time.time_since_epoch()).count();
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = const_cast<char*>(name.data()),
                                                     .length = name.size()};
  auto child_module_span = config_->on_span_spawn_child_(in_module_span_, name_buf, start_time_ns);
  if (child_module_span == nullptr) {
    return std::make_unique<Tracing::NullSpan>();
  }
  return std::make_unique<DynamicModuleSpan>(config_, child_module_span, trace_context_);
}

void DynamicModuleSpan::setSampled(bool sampled) {
  config_->on_span_set_sampled_(in_module_span_, sampled);
}

bool DynamicModuleSpan::useLocalDecision() const {
  return config_->on_span_use_local_decision_(in_module_span_);
}

std::string DynamicModuleSpan::getBaggage(absl::string_view key) {
  envoy_dynamic_module_type_envoy_buffer key_buf = {.ptr = const_cast<char*>(key.data()),
                                                    .length = key.size()};
  envoy_dynamic_module_type_module_buffer value_out = {.ptr = nullptr, .length = 0};
  if (config_->on_span_get_baggage_(in_module_span_, key_buf, &value_out) &&
      value_out.ptr != nullptr) {
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::string(value_out.ptr, value_out.length);
  }
  return {};
}

void DynamicModuleSpan::setBaggage(absl::string_view key, absl::string_view value) {
  envoy_dynamic_module_type_envoy_buffer key_buf = {.ptr = const_cast<char*>(key.data()),
                                                    .length = key.size()};
  envoy_dynamic_module_type_envoy_buffer val_buf = {.ptr = const_cast<char*>(value.data()),
                                                    .length = value.size()};
  config_->on_span_set_baggage_(in_module_span_, key_buf, val_buf);
}

std::string DynamicModuleSpan::getTraceId() const {
  envoy_dynamic_module_type_module_buffer value_out = {.ptr = nullptr, .length = 0};
  if (config_->on_span_get_trace_id_(in_module_span_, &value_out) && value_out.ptr != nullptr) {
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::string(value_out.ptr, value_out.length);
  }
  return {};
}

std::string DynamicModuleSpan::getSpanId() const {
  envoy_dynamic_module_type_module_buffer value_out = {.ptr = nullptr, .length = 0};
  if (config_->on_span_get_span_id_(in_module_span_, &value_out) && value_out.ptr != nullptr) {
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::string(value_out.ptr, value_out.length);
  }
  return {};
}

// =============================================================================
// DynamicModuleDriver
// =============================================================================

DynamicModuleDriver::DynamicModuleDriver(DynamicModuleTracerConfigSharedPtr config)
    : config_(std::move(config)) {}

Tracing::SpanPtr DynamicModuleDriver::startSpan(const Tracing::Config&,
                                                Tracing::TraceContext& trace_context,
                                                const StreamInfo::StreamInfo&,
                                                const std::string& operation_name,
                                                Tracing::Decision tracing_decision) {
  // Create a temporary span wrapper so the module can access trace context via callbacks.
  auto span = std::make_unique<DynamicModuleSpan>(config_, nullptr, &trace_context);

  envoy_dynamic_module_type_envoy_buffer op_buf = {.ptr = const_cast<char*>(operation_name.data()),
                                                   .length = operation_name.size()};

  auto in_module_span =
      config_->on_start_span_(config_->in_module_config_, static_cast<void*>(span.get()), op_buf,
                              tracing_decision.traced, toAbiReason(tracing_decision.reason));
  if (in_module_span == nullptr) {
    return std::make_unique<Tracing::NullSpan>();
  }

  span->in_module_span_ = in_module_span;
  return span;
}

} // namespace DynamicModules
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
