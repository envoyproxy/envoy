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

static Envoy::Stats::StatNameTagVector buildTagsForTracerMetric(
    Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig& config,
    const Envoy::Stats::StatNameVec& label_names,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length) {
  ASSERT(label_values_length == label_names.size());
  Envoy::Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; i++) {
    absl::string_view label_value_view(label_values[i].ptr, label_values[i].length);
    auto label_value = config.stat_name_pool_.add(label_value_view);
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
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->stat_name_pool_.add(name_view);

  if (label_names_length == 0) {
    auto& counter = config->stats_scope_->counterFromStatName(stat_name);
    *counter_id_ptr = config->addCounter(
        Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::ModuleCounterHandle(
            counter));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *counter_id_ptr = config->addCounterVec(
      Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::ModuleCounterVecHandle(
          stat_name, label_names_vec));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto* config = getConfig(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->stat_name_pool_.add(name_view);
  auto import_mode = Envoy::Stats::Gauge::ImportMode::NeverImport;

  if (label_names_length == 0) {
    auto& gauge = config->stats_scope_->gaugeFromStatName(stat_name, import_mode);
    *gauge_id_ptr = config->addGauge(
        Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::ModuleGaugeHandle(
            gauge));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *gauge_id_ptr = config->addGaugeVec(
      Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::ModuleGaugeVecHandle(
          stat_name, label_names_vec, import_mode));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_histogram(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto* config = getConfig(config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  auto stat_name = config->stat_name_pool_.add(name_view);
  auto unit = Envoy::Stats::Histogram::Unit::Unspecified;

  if (label_names_length == 0) {
    auto& histogram = config->stats_scope_->histogramFromStatName(stat_name, unit);
    *histogram_id_ptr =
        config->addHistogram(Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::
                                 ModuleHistogramHandle(histogram));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *histogram_id_ptr = config->addHistogramVec(
      Envoy::Extensions::Tracers::DynamicModules::DynamicModuleTracerConfig::
          ModuleHistogramVecHandle(stat_name, label_names_vec, unit));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_increment_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);

  if (label_values_length == 0) {
    auto counter = config->getCounterById(id);
    if (!counter.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    counter->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto counter = config->getCounterVecById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != counter->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForTracerMetric(*config, counter->getLabelNames(), label_values,
                                       label_values_length);
  counter->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_record_histogram_value(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);

  if (label_values_length == 0) {
    auto histogram = config->getHistogramById(id);
    if (!histogram.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    histogram->recordValue(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto histogram = config->getHistogramVecById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != histogram->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForTracerMetric(*config, histogram->getLabelNames(), label_values,
                                       label_values_length);
  histogram->recordValue(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_set_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = getConfig(config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->set(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForTracerMetric(*config, gauge->getLabelNames(), label_values, label_values_length);
  gauge->set(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"
