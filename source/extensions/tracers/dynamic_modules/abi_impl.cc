// NOLINT(namespace-envoy)

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/tracers/dynamic_modules/tracer_config.h"

namespace {

Envoy::Extensions::Tracers::DynamicModules::DynamicModuleSpan*
getSpan(envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr) {
  return static_cast<Envoy::Extensions::Tracers::DynamicModules::DynamicModuleSpan*>(
      span_envoy_ptr);
}

Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig*
getConfig(envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr) {
  return static_cast<Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig*>(
      config_envoy_ptr);
}

} // namespace

extern "C" {

// ----------------------- Trace Context Operations ---------------------------

bool envoy_dynamic_module_callback_tracer_get_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* span = getSpan(span_envoy_ptr);
  auto* ctx = span->traceContext();
  if (ctx == nullptr) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);
  auto result = ctx->get(key_view);
  if (!result.has_value()) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  *value_out = {.ptr = const_cast<char*>(result.value().data()), .length = result.value().size()};
  return true;
}

void envoy_dynamic_module_callback_tracer_set_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* span = getSpan(span_envoy_ptr);
  auto* ctx = span->traceContext();
  if (ctx == nullptr) {
    return;
  }
  ctx->set(absl::string_view(key.ptr, key.length), absl::string_view(value.ptr, value.length));
}

void envoy_dynamic_module_callback_tracer_remove_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key) {
  auto* span = getSpan(span_envoy_ptr);
  auto* ctx = span->traceContext();
  if (ctx == nullptr) {
    return;
  }
  ctx->remove(absl::string_view(key.ptr, key.length));
}

bool envoy_dynamic_module_callback_tracer_get_trace_context_protocol(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* span = getSpan(span_envoy_ptr);
  auto* ctx = span->traceContext();
  if (ctx == nullptr) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  auto protocol = ctx->protocol();
  if (protocol.empty()) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  *value_out = {.ptr = const_cast<char*>(protocol.data()), .length = protocol.size()};
  return true;
}

bool envoy_dynamic_module_callback_tracer_get_trace_context_host(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* span = getSpan(span_envoy_ptr);
  auto* ctx = span->traceContext();
  if (ctx == nullptr) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  auto host = ctx->host();
  if (host.empty()) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  *value_out = {.ptr = const_cast<char*>(host.data()), .length = host.size()};
  return true;
}

bool envoy_dynamic_module_callback_tracer_get_trace_context_path(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* span = getSpan(span_envoy_ptr);
  auto* ctx = span->traceContext();
  if (ctx == nullptr) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  auto path = ctx->path();
  if (path.empty()) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  *value_out = {.ptr = const_cast<char*>(path.data()), .length = path.size()};
  return true;
}

bool envoy_dynamic_module_callback_tracer_get_trace_context_method(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* span = getSpan(span_envoy_ptr);
  auto* ctx = span->traceContext();
  if (ctx == nullptr) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  auto method = ctx->method();
  if (method.empty()) {
    *value_out = {.ptr = nullptr, .length = 0};
    return false;
  }
  *value_out = {.ptr = const_cast<char*>(method.data()), .length = method.size()};
  return true;
}

// ----------------------- Metrics Operations ----------------------------------

size_t envoy_dynamic_module_callback_tracer_define_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* config = getConfig(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->stat_name_pool_.add(name_view);
  auto& counter = config->stats_scope_->counterFromStatName(stat_name);
  return config->addCounter(
      Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::ModuleCounterHandle(
          counter));
}

size_t envoy_dynamic_module_callback_tracer_define_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* config = getConfig(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->stat_name_pool_.add(name_view);
  auto& gauge = config->stats_scope_->gaugeFromStatName(
      stat_name, Envoy::Stats::Gauge::ImportMode::NeverImport);
  return config->addGauge(
      Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::ModuleGaugeHandle(
          gauge));
}

size_t envoy_dynamic_module_callback_tracer_define_histogram(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* config = getConfig(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->stat_name_pool_.add(name_view);
  auto& histogram = config->stats_scope_->histogramFromStatName(
      stat_name, Envoy::Stats::Histogram::Unit::Unspecified);
  return config->addHistogram(
      Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::ModuleHistogramHandle(
          histogram));
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_increment_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t counter_id,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);
  auto counter = config->getCounterById(counter_id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_record_histogram_value(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t histogram_id,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);
  auto histogram = config->getHistogramById(histogram_id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_set_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t gauge_id,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);
  auto gauge = config->getGaugeById(gauge_id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"
