// NOLINT(namespace-envoy)

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/tracers/dynamic_modules/tracer_config.h"

using Envoy::Extensions::DynamicModules::MetricRegistry;

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

// Builds the tag vector using a caller-owned stack-local pool so the registry's shared stat name
// pool is not mutated from worker threads. Returned tags borrow storage from `dynamic_pool`.
static Envoy::Stats::StatNameTagVector buildTagsForTracerMetric(
    Envoy::Stats::StatNameDynamicPool& dynamic_pool, const Envoy::Stats::StatNameVec& label_names,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length) {
  ASSERT(label_values_length == label_names.size());
  Envoy::Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; i++) {
    absl::string_view label_value_view(label_values[i].ptr, label_values[i].length);
    auto label_value = dynamic_pool.add(label_value_view);
    tags.push_back(Envoy::Stats::StatNameTag(label_names[i], label_value));
  }
  return tags;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto* config = getConfig(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->metrics().statNamePool().add(name_view);

  if (label_names_length == 0) {
    auto& counter = config->metrics().scope().counterFromStatName(stat_name);
    *counter_id_ptr = config->metrics().addCounter(MetricRegistry::CounterHandle(counter));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->metrics().statNamePool().add(label_name_view));
  }
  *counter_id_ptr = config->metrics().addCounterVec(
      MetricRegistry::CounterVecHandle(stat_name, std::move(label_names_vec)));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto* config = getConfig(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->metrics().statNamePool().add(name_view);
  auto import_mode = Envoy::Stats::Gauge::ImportMode::NeverImport;

  if (label_names_length == 0) {
    auto& gauge = config->metrics().scope().gaugeFromStatName(stat_name, import_mode);
    *gauge_id_ptr = config->metrics().addGauge(MetricRegistry::GaugeHandle(gauge));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->metrics().statNamePool().add(label_name_view));
  }
  *gauge_id_ptr = config->metrics().addGaugeVec(
      MetricRegistry::GaugeVecHandle(stat_name, std::move(label_names_vec), import_mode));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_histogram(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto* config = getConfig(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->metrics().statNamePool().add(name_view);
  auto unit = Envoy::Stats::Histogram::Unit::Unspecified;

  if (label_names_length == 0) {
    auto& histogram = config->metrics().scope().histogramFromStatName(stat_name, unit);
    *histogram_id_ptr = config->metrics().addHistogram(MetricRegistry::HistogramHandle(histogram));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->metrics().statNamePool().add(label_name_view));
  }
  *histogram_id_ptr = config->metrics().addHistogramVec(
      MetricRegistry::HistogramVecHandle(stat_name, std::move(label_names_vec), unit));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_increment_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);

  if (label_values_length == 0) {
    auto counter = config->metrics().getCounterById(id);
    if (!counter.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    counter->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto counter = config->metrics().getCounterVecById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != counter->labelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->metrics().scope().symbolTable());
  auto tags = buildTagsForTracerMetric(dynamic_pool, counter->labelNames(), label_values,
                                       label_values_length);
  counter->add(config->metrics().scope(), tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_record_histogram_value(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);

  if (label_values_length == 0) {
    auto histogram = config->metrics().getHistogramById(id);
    if (!histogram.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    histogram->recordValue(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto histogram = config->metrics().getHistogramVecById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != histogram->labelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->metrics().scope().symbolTable());
  auto tags = buildTagsForTracerMetric(dynamic_pool, histogram->labelNames(), label_values,
                                       label_values_length);
  histogram->recordValue(config->metrics().scope(), tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_set_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->metrics().getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->set(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto gauge = config->metrics().getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->labelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->metrics().scope().symbolTable());
  auto tags = buildTagsForTracerMetric(dynamic_pool, gauge->labelNames(), label_values,
                                       label_values_length);
  gauge->set(config->metrics().scope(), tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"
