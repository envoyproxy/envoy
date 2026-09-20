#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

#include "envoy/router/router_ratelimit.h"

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/router/route_specifiers/dynamic_modules/route_specifier.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::MetricRegistry;

namespace {

RouteSpecifierContext*
routeSpecifierContext(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ptr) {
  return static_cast<RouteSpecifierContext*>(ptr);
}

// The context of a setter, or nullptr when the setters do nothing because the decision has already
// been made.
RouteSpecifierContext*
settableContext(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ptr) {
  auto* context = routeSpecifierContext(ptr);
  return context->setters_enabled ? context : nullptr;
}

DynamicModuleRouteSpecifierConfig*
routeSpecifierConfig(envoy_dynamic_module_type_route_specifier_config_envoy_ptr ptr) {
  return static_cast<DynamicModuleRouteSpecifierConfig*>(ptr);
}

absl::string_view toStringView(envoy_dynamic_module_type_module_buffer buffer) {
  if (buffer.ptr == nullptr) {
    return {};
  }
  return {buffer.ptr, buffer.length};
}

void setEnvoyBuffer(envoy_dynamic_module_type_envoy_buffer* result, absl::string_view value) {
  result->ptr = value.data();
  result->length = value.size();
}

// Durations cross the ABI boundary as unsigned, so clamp them to the ceiling that
// TimerUtils::durationToTimeval already imposes. Clamping to the millisecond representation maximum
// instead would overflow the float scale factor that the stream idle timer applies, which reaches
// the timer as a negative duration and fires it immediately.
std::chrono::milliseconds toMilliseconds(uint64_t value_ms) {
  using Rep = std::chrono::milliseconds::rep;
  constexpr uint64_t max_ms = static_cast<uint64_t>(
      std::chrono::milliseconds(std::chrono::seconds(std::numeric_limits<int32_t>::max())).count());
  return std::chrono::milliseconds(static_cast<Rep>(std::min(value_ms, max_ms)));
}

// A null array with a non-zero length is a module bug, so the call is rejected rather than
// followed.
bool labelsPresent(envoy_dynamic_module_type_module_buffer* labels, size_t labels_length) {
  return labels_length == 0 || labels != nullptr;
}

Envoy::Stats::StatNameTagVector buildTagsForRouteSpecifierMetric(
    Envoy::Stats::StatNameDynamicPool& dynamic_pool, const Envoy::Stats::StatNameVec& label_names,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length) {
  ASSERT(label_values_length == label_names.size());
  Envoy::Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; i++) {
    auto label_value = dynamic_pool.add(toStringView(label_values[i]));
    tags.push_back(Envoy::Stats::StatNameTag(label_names[i], label_value));
  }
  return tags;
}

// Returns a mutable handle to the route metadata value the module is setting, creating the
// namespace and key when they are absent, or nullptr when the namespace is not allowed.
Protobuf::Value* mutableRouteMetadataValue(RouteSpecifierContext* context,
                                           envoy_dynamic_module_type_module_buffer ns,
                                           envoy_dynamic_module_type_module_buffer key) {
  if (context == nullptr || !context->config.metadataNamespaceAllowed(toStringView(ns))) {
    return nullptr;
  }
  return &Envoy::Config::Metadata::mutableMetadataValue(context->overrides.route_metadata,
                                                        std::string(toStringView(ns)),
                                                        std::string(toStringView(key)));
}

bool knownAppendAction(envoy_dynamic_module_type_route_specifier_header_append_action action) {
  switch (action) {
  case envoy_dynamic_module_type_route_specifier_header_append_action_AppendIfExistsOrAdd:
  case envoy_dynamic_module_type_route_specifier_header_append_action_AddIfAbsent:
  case envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExistsOrAdd:
  case envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExists:
    return true;
  }
  return false;
}

bool recordHeaderMutation(RouteSpecifierContext* context,
                          envoy_dynamic_module_type_module_buffer key,
                          envoy_dynamic_module_type_module_buffer value,
                          envoy_dynamic_module_type_route_specifier_header_append_action action,
                          std::vector<HeaderMutation>& mutations) {
  const absl::string_view key_view = toStringView(key);
  const absl::string_view value_view = toStringView(value);
  if (!knownAppendAction(action) || key_view.empty() || absl::StartsWith(key_view, ":") ||
      !Http::HeaderUtility::headerNameIsValid(key_view) ||
      !Http::HeaderUtility::headerValueIsValid(value_view)) {
    return false;
  }
  if (context != nullptr) {
    mutations.push_back({Http::LowerCaseString(key_view), std::string(value_view), action});
  }
  return true;
}

bool recordHeaderRemoval(RouteSpecifierContext* context,
                         envoy_dynamic_module_type_module_buffer key,
                         std::vector<Http::LowerCaseString>& removals) {
  const absl::string_view key_view = toStringView(key);
  if (key_view.empty() || absl::StartsWith(key_view, ":") ||
      !Http::HeaderUtility::headerNameIsValid(key_view)) {
    return false;
  }
  if (context != nullptr) {
    removals.emplace_back(key_view);
  }
  return true;
}

// The gauge operations differ only in what they do with the value, so they share this lookup.
template <typename OnGauge, typename OnGaugeVec>
envoy_dynamic_module_type_metrics_result
withGauge(envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
          envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
          OnGauge on_gauge, OnGaugeVec on_gauge_vec) {
  auto* config = routeSpecifierConfig(config_envoy_ptr);
  if (!labelsPresent(label_values, label_values_length)) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }

  if (label_values_length == 0) {
    auto gauge = config->metrics().getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->metrics().getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    on_gauge(*gauge);
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
  auto tags = buildTagsForRouteSpecifierMetric(dynamic_pool, gauge->labelNames(), label_values,
                                               label_values_length);
  on_gauge_vec(*gauge, config->metrics().scope(), tags);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // namespace

extern "C" {

// ------------------------------- Configuration -------------------------------

size_t envoy_dynamic_module_callback_route_specifier_config_get_template_count(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr) {
  return routeSpecifierConfig(config_envoy_ptr)->templateIds().size();
}

bool envoy_dynamic_module_callback_route_specifier_config_get_template_id(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr, size_t index,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* config = routeSpecifierConfig(config_envoy_ptr);
  if (index >= config->templateIds().size()) {
    return false;
  }
  setEnvoyBuffer(result, config->templateIds()[index]);
  return true;
}

envoy_dynamic_module_type_route_specifier_route_kind
envoy_dynamic_module_callback_route_specifier_config_get_template_kind(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer template_id) {
  const auto* route_template =
      routeSpecifierConfig(config_envoy_ptr)->routeTemplate(toStringView(template_id));
  return route_template != nullptr ? route_template->kind
                                   : envoy_dynamic_module_type_route_specifier_route_kind_None;
}

bool envoy_dynamic_module_callback_route_specifier_config_has_route_action_override(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  return routeSpecifierConfig(config_envoy_ptr)->routeActionOverride(toStringView(name)) != nullptr;
}

bool envoy_dynamic_module_callback_route_specifier_config_is_shadow_mode(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr) {
  return routeSpecifierConfig(config_envoy_ptr)->shadow().has_value();
}

bool envoy_dynamic_module_callback_route_specifier_config_register_route_template(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer template_id,
    envoy_dynamic_module_type_module_buffer serialized_route) {
  return routeSpecifierConfig(config_envoy_ptr)
      ->registerRouteTemplate(toStringView(template_id), toStringView(serialized_route));
}

// ---------------------------------- Metrics ----------------------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_define_counter(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto* config = routeSpecifierConfig(config_envoy_ptr);
  if (config->stat_creation_frozen_.load(std::memory_order_acquire)) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  if (!labelsPresent(label_names, label_names_length)) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatName main_stat_name = config->metrics().statNamePool().add(toStringView(name));

  if (label_names_length == 0) {
    Envoy::Stats::Counter& counter =
        Envoy::Stats::Utility::counterFromStatNames(config->metrics().scope(), {main_stat_name});
    *counter_id_ptr = config->metrics().addCounter(MetricRegistry::CounterHandle(counter));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    label_names_vec.push_back(config->metrics().statNamePool().add(toStringView(label_names[i])));
  }
  *counter_id_ptr = config->metrics().addCounterVec(
      MetricRegistry::CounterVecHandle(main_stat_name, std::move(label_names_vec)));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_increment_counter(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = routeSpecifierConfig(config_envoy_ptr);
  if (!labelsPresent(label_values, label_values_length)) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }

  if (label_values_length == 0) {
    auto counter = config->metrics().getCounterById(id);
    if (!counter.has_value()) {
      if (config->metrics().getCounterVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  auto tags = buildTagsForRouteSpecifierMetric(dynamic_pool, counter->labelNames(), label_values,
                                               label_values_length);
  counter->add(config->metrics().scope(), tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_define_gauge(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto* config = routeSpecifierConfig(config_envoy_ptr);
  if (config->stat_creation_frozen_.load(std::memory_order_acquire)) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  if (!labelsPresent(label_names, label_names_length)) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatName main_stat_name = config->metrics().statNamePool().add(toStringView(name));
  Envoy::Stats::Gauge::ImportMode import_mode = Envoy::Stats::Gauge::ImportMode::Accumulate;

  if (label_names_length == 0) {
    Envoy::Stats::Gauge& gauge = Envoy::Stats::Utility::gaugeFromStatNames(
        config->metrics().scope(), {main_stat_name}, import_mode);
    *gauge_id_ptr = config->metrics().addGauge(MetricRegistry::GaugeHandle(gauge));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    label_names_vec.push_back(config->metrics().statNamePool().add(toStringView(label_names[i])));
  }
  *gauge_id_ptr = config->metrics().addGaugeVec(
      MetricRegistry::GaugeVecHandle(main_stat_name, std::move(label_names_vec), import_mode));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_set_gauge(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return withGauge(
      config_envoy_ptr, id, label_values, label_values_length,
      [value](auto& gauge) { gauge.set(value); },
      [value](auto& gauge, auto& scope, const auto& tags) { gauge.set(scope, tags, value); });
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_increment_gauge(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return withGauge(
      config_envoy_ptr, id, label_values, label_values_length,
      [value](auto& gauge) { gauge.increase(value); },
      [value](auto& gauge, auto& scope, const auto& tags) { gauge.increase(scope, tags, value); });
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_decrement_gauge(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  return withGauge(
      config_envoy_ptr, id, label_values, label_values_length,
      [value](auto& gauge) { gauge.decrease(value); },
      [value](auto& gauge, auto& scope, const auto& tags) { gauge.decrease(scope, tags, value); });
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_define_histogram(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto* config = routeSpecifierConfig(config_envoy_ptr);
  if (config->stat_creation_frozen_.load(std::memory_order_acquire)) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  if (!labelsPresent(label_names, label_names_length)) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatName main_stat_name = config->metrics().statNamePool().add(toStringView(name));
  Envoy::Stats::Histogram::Unit unit = Envoy::Stats::Histogram::Unit::Unspecified;

  if (label_names_length == 0) {
    Envoy::Stats::Histogram& histogram = Envoy::Stats::Utility::histogramFromStatNames(
        config->metrics().scope(), {main_stat_name}, unit);
    *histogram_id_ptr = config->metrics().addHistogram(MetricRegistry::HistogramHandle(histogram));
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    label_names_vec.push_back(config->metrics().statNamePool().add(toStringView(label_names[i])));
  }
  *histogram_id_ptr = config->metrics().addHistogramVec(
      MetricRegistry::HistogramVecHandle(main_stat_name, std::move(label_names_vec), unit));
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_route_specifier_config_record_histogram_value(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = routeSpecifierConfig(config_envoy_ptr);
  if (!labelsPresent(label_values, label_values_length)) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }

  if (label_values_length == 0) {
    auto histogram = config->metrics().getHistogramById(id);
    if (!histogram.has_value()) {
      if (config->metrics().getHistogramVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  auto tags = buildTagsForRouteSpecifierMetric(dynamic_pool, histogram->labelNames(), label_values,
                                               label_values_length);
  histogram->recordValue(config->metrics().scope(), tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

// -------------------------------- Request state ------------------------------

size_t envoy_dynamic_module_callback_route_specifier_get_request_headers_size(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr) {
  return routeSpecifierContext(context_envoy_ptr)->headers.size();
}

bool envoy_dynamic_module_callback_route_specifier_get_request_headers(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  return ContextAccessor::getHeaders(routeSpecifierContext(context_envoy_ptr)->headers,
                                     result_headers);
}

bool envoy_dynamic_module_callback_route_specifier_get_request_header_value(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  return ContextAccessor::getHeaderValue(routeSpecifierContext(context_envoy_ptr)->headers, key,
                                         result, index, total_count_out);
}

bool envoy_dynamic_module_callback_route_specifier_get_attribute_string(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result) {
  return ContextAccessor::getAttributeString(routeSpecifierContext(context_envoy_ptr)->stream_info,
                                             attribute_id, result);
}

bool envoy_dynamic_module_callback_route_specifier_get_attribute_int(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  ContextAccessor::HttpAttributeContext http_context{&context->headers, nullptr, nullptr, nullptr};
  return ContextAccessor::getAttributeInt(context->stream_info, attribute_id, result,
                                          &http_context);
}

bool envoy_dynamic_module_callback_route_specifier_get_attribute_bool(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result) {
  return ContextAccessor::getAttributeBool(routeSpecifierContext(context_envoy_ptr)->stream_info,
                                           attribute_id, result);
}

bool envoy_dynamic_module_callback_route_specifier_get_dynamic_metadata(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result) {
  return ContextAccessor::getDynamicMetadata(routeSpecifierContext(context_envoy_ptr)->stream_info,
                                             filter_name, path, result);
}

bool envoy_dynamic_module_callback_route_specifier_get_dynamic_metadata_number(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, double* result) {
  return ContextAccessor::getDynamicMetadataNumber(
      routeSpecifierContext(context_envoy_ptr)->stream_info, filter_name, path, result);
}

bool envoy_dynamic_module_callback_route_specifier_get_dynamic_metadata_bool(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, bool* result) {
  return ContextAccessor::getDynamicMetadataBool(
      routeSpecifierContext(context_envoy_ptr)->stream_info, filter_name, path, result);
}

bool envoy_dynamic_module_callback_route_specifier_get_filter_state_bytes(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  return ContextAccessor::getFilterStateBytes(routeSpecifierContext(context_envoy_ptr)->stream_info,
                                              key, result);
}

uint64_t envoy_dynamic_module_callback_route_specifier_get_random_value(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr) {
  return routeSpecifierContext(context_envoy_ptr)->random_value;
}

bool envoy_dynamic_module_callback_route_specifier_get_cluster_host_count(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name, uint32_t priority, size_t* total_count,
    size_t* healthy_count, size_t* degraded_count) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  auto thread_local_cluster =
      context->config.clusterManager().getThreadLocalCluster(toStringView(cluster_name));
  if (thread_local_cluster == nullptr) {
    return false;
  }
  const auto& host_sets = thread_local_cluster->prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return false;
  }
  const auto& host_set = *host_sets[priority];
  if (total_count != nullptr) {
    *total_count = host_set.hosts().size();
  }
  if (healthy_count != nullptr) {
    *healthy_count = host_set.healthyHosts().size();
  }
  if (degraded_count != nullptr) {
    *degraded_count = host_set.degradedHosts().size();
  }
  return true;
}

// --------------------------------- Input route -------------------------------

bool envoy_dynamic_module_callback_route_specifier_get_input_route(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_route_specifier_input_route* result) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr) {
    return false;
  }
  *result = {};
  const auto* entry = route->routeEntry();
  result->kind = entry != nullptr
                     ? envoy_dynamic_module_type_route_specifier_route_kind_RouteEntry
                     : envoy_dynamic_module_type_route_specifier_route_kind_DirectResponse;
  setEnvoyBuffer(&result->name, route->routeName());
  setEnvoyBuffer(&result->virtual_host_name, route->virtualHost().name());
  const auto& metadata = route->metadata();
  result->has_metadata =
      !metadata.filter_metadata().empty() || !metadata.typed_filter_metadata().empty();
  if (entry != nullptr) {
    setEnvoyBuffer(&result->cluster_name, entry->clusterName());
    result->timeout_ms = static_cast<uint64_t>(entry->timeout().count());
    if (const auto idle_timeout = entry->idleTimeout(); idle_timeout.has_value()) {
      result->has_idle_timeout = true;
      result->idle_timeout_ms = static_cast<uint64_t>(idle_timeout->count());
    }
    if (const auto max_stream_duration = entry->maxStreamDuration();
        max_stream_duration.has_value()) {
      result->has_max_stream_duration = true;
      result->max_stream_duration_ms = static_cast<uint64_t>(max_stream_duration->count());
    }
    result->priority = entry->priority() == Upstream::ResourcePriority::High
                           ? envoy_dynamic_module_type_resource_priority_High
                           : envoy_dynamic_module_type_resource_priority_Default;
    result->request_body_buffer_limit = entry->requestBodyBufferLimit();
    result->cluster_not_found_response_code =
        static_cast<uint32_t>(entry->clusterNotFoundResponseCode());
    result->has_metadata_match = entry->metadataMatchCriteria() != nullptr;
    result->has_hash_policy = entry->hashPolicy() != nullptr;
    result->has_rate_limits = !entry->rateLimitPolicy().empty();
    result->request_mirror_policies_count = entry->shadowPolicies().size();
  } else {
    result->response_code = static_cast<uint32_t>(route->directResponseEntry()->responseCode());
  }
  return true;
}

envoy_dynamic_module_type_route_specifier_route_kind
envoy_dynamic_module_callback_route_specifier_get_input_route_kind(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr) {
    return envoy_dynamic_module_type_route_specifier_route_kind_None;
  }
  return route->routeEntry() != nullptr
             ? envoy_dynamic_module_type_route_specifier_route_kind_RouteEntry
             : envoy_dynamic_module_type_route_specifier_route_kind_DirectResponse;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_name(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr) {
    return false;
  }
  setEnvoyBuffer(result, route->routeName());
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_virtual_host_name(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr) {
    return false;
  }
  setEnvoyBuffer(result, route->virtualHost().name());
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_cluster_name(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return false;
  }
  setEnvoyBuffer(result, route->routeEntry()->clusterName());
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_timeout(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t* timeout_ms) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return false;
  }
  *timeout_ms = static_cast<uint64_t>(route->routeEntry()->timeout().count());
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_response_code(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    uint32_t* status_code) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr || route->directResponseEntry() == nullptr) {
    return false;
  }
  *status_code = static_cast<uint32_t>(route->directResponseEntry()->responseCode());
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_redirect_location(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  const auto& route = context->currentRoute();
  if (route == nullptr || route->directResponseEntry() == nullptr) {
    return false;
  }
  // Built once per route so repeated reads for the same route return the same buffer, and the
  // buffers of earlier routes stay valid after the current route changes.
  if (context->input_redirect_locations.empty() || context->input_redirect_route != route.get()) {
    context->input_redirect_locations.push_back(
        route->directResponseEntry()->newUri(context->headers, context->stream_info));
    context->input_redirect_route = route.get();
  }
  setEnvoyBuffer(result, context->input_redirect_locations.back());
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_metadata(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr) {
    return false;
  }
  const auto& value = Envoy::Config::Metadata::metadataValue(
      &route->metadata(), std::string(toStringView(ns)), std::string(toStringView(key)));
  if (value.kind_case() != Protobuf::Value::kStringValue) {
    return false;
  }
  setEnvoyBuffer(result, value.string_value());
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_input_route_metadata_number(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double* result) {
  const auto& route = routeSpecifierContext(context_envoy_ptr)->currentRoute();
  if (route == nullptr) {
    return false;
  }
  const auto& value = Envoy::Config::Metadata::metadataValue(
      &route->metadata(), std::string(toStringView(ns)), std::string(toStringView(key)));
  if (value.kind_case() != Protobuf::Value::kNumberValue) {
    return false;
  }
  *result = value.number_value();
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_get_selected_template_id(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  if (context->selected_template == nullptr) {
    return false;
  }
  setEnvoyBuffer(result, context->selected_template->id);
  return true;
}

// ---------------------------------- Decision ---------------------------------

bool envoy_dynamic_module_callback_route_specifier_set_template(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer template_id) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  const absl::string_view id = toStringView(template_id);
  const auto* route_template = context->config.routeTemplate(id);
  if (route_template == nullptr) {
    ENVOY_LOG_MISC(debug, "dynamic module route specifier selected unknown template '{}'", id);
    return false;
  }
  if (context->setters_enabled) {
    context->selected_template = route_template;
    // Evaluate the template now so that the getters reflect the route being produced and the
    // decision reuses it rather than matching a second time. A null result means the match did not
    // hold, which resolve() turns into a template match failure.
    context->selected_route =
        route_template->route->match(context->headers, context->stream_info, context->random_value);
  }
  return true;
}

void envoy_dynamic_module_callback_route_specifier_set_chain_status(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_route_specifier_chain_status status) {
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->chain_status = status;
  }
}

bool envoy_dynamic_module_callback_route_specifier_set_cluster_name(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  const absl::string_view name = toStringView(cluster_name);
  if (name.empty() || !Http::HeaderUtility::headerValueIsValid(name) ||
      !context->config.clusterNameAllowed(name)) {
    ENVOY_LOG_MISC(debug, "dynamic module route specifier rejected cluster '{}'", name);
    return false;
  }
  if (context->setters_enabled) {
    context->overrides.cluster_name.assign(name.data(), name.size());
  }
  return true;
}

void envoy_dynamic_module_callback_route_specifier_set_timeout(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t timeout_ms) {
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.timeout = toMilliseconds(timeout_ms);
  }
}

void envoy_dynamic_module_callback_route_specifier_set_idle_timeout(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t idle_timeout_ms) {
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.idle_timeout = toMilliseconds(idle_timeout_ms);
  }
}

void envoy_dynamic_module_callback_route_specifier_set_max_stream_duration(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t max_stream_duration_ms) {
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.max_stream_duration = toMilliseconds(max_stream_duration_ms);
  }
}

void envoy_dynamic_module_callback_route_specifier_set_request_body_buffer_limit(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t limit_bytes) {
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.request_body_buffer_limit = limit_bytes;
  }
}

void envoy_dynamic_module_callback_route_specifier_set_priority(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_resource_priority priority) {
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.priority = priority == envoy_dynamic_module_type_resource_priority_High
                                      ? Upstream::ResourcePriority::High
                                      : Upstream::ResourcePriority::Default;
  }
}

bool envoy_dynamic_module_callback_route_specifier_set_cluster_not_found_response_code(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    uint32_t status_code) {
  if (status_code < 200 || status_code >= 600) {
    return false;
  }
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.cluster_not_found_response_code = static_cast<Http::Code>(status_code);
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_route_action_override(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  const auto* entry = context->config.routeActionOverride(toStringView(name));
  if (entry == nullptr) {
    ENVOY_LOG_MISC(debug,
                   "dynamic module route specifier selected unknown route action override "
                   "'{}'",
                   toStringView(name));
    return false;
  }
  if (context->setters_enabled) {
    context->overrides.route_action_override = entry;
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_route_metadata_string(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  if (!context->config.metadataNamespaceAllowed(toStringView(ns))) {
    return false;
  }
  if (Protobuf::Value* target =
          mutableRouteMetadataValue(settableContext(context_envoy_ptr), ns, key);
      target != nullptr) {
    target->set_string_value(std::string(toStringView(value)));
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_route_metadata_number(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double value) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  if (!context->config.metadataNamespaceAllowed(toStringView(ns))) {
    return false;
  }
  if (Protobuf::Value* target =
          mutableRouteMetadataValue(settableContext(context_envoy_ptr), ns, key);
      target != nullptr) {
    target->set_number_value(value);
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_route_metadata_bool(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    bool value) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  if (!context->config.metadataNamespaceAllowed(toStringView(ns))) {
    return false;
  }
  if (Protobuf::Value* target =
          mutableRouteMetadataValue(settableContext(context_envoy_ptr), ns, key);
      target != nullptr) {
    target->set_bool_value(value);
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_route_typed_metadata(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns,
    envoy_dynamic_module_type_module_buffer serialized_any) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  const absl::string_view name = toStringView(ns);
  if (!context->config.metadataNamespaceAllowed(name)) {
    return false;
  }
  const absl::string_view serialized = toStringView(serialized_any);
  Protobuf::Any any;
  if (!any.ParseFromString(serialized)) {
    return false;
  }
  if (context->setters_enabled) {
    (*context->overrides.route_metadata.mutable_typed_filter_metadata())[std::string(name)] =
        std::move(any);
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_filter_disabled(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name, bool disabled) {
  auto* context = routeSpecifierContext(context_envoy_ptr);
  const absl::string_view name = toStringView(filter_name);
  if (name.empty() || !context->config.filterNameAllowed(name)) {
    ENVOY_LOG_MISC(debug, "dynamic module route specifier rejected filter '{}'", name);
    return false;
  }
  if (context->setters_enabled) {
    context->overrides.filter_disabled[std::string(name)] = disabled;
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_path(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer path) {
  const absl::string_view value = toStringView(path);
  if (!absl::StartsWith(value, "/") || !Http::HeaderUtility::headerValueIsValid(value)) {
    return false;
  }
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.path = std::string(value);
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_set_host(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer host) {
  const absl::string_view value = toStringView(host);
  if (value.empty() || !Http::HeaderUtility::authorityIsValid(value)) {
    return false;
  }
  if (auto* context = settableContext(context_envoy_ptr); context != nullptr) {
    context->overrides.host = std::string(value);
  }
  return true;
}

bool envoy_dynamic_module_callback_route_specifier_add_request_header(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value,
    envoy_dynamic_module_type_route_specifier_header_append_action action) {
  auto* context = settableContext(context_envoy_ptr);
  return recordHeaderMutation(
      context, key, value, action,
      context != nullptr
          ? context->overrides.request_headers_to_add
          : routeSpecifierContext(context_envoy_ptr)->overrides.request_headers_to_add);
}

bool envoy_dynamic_module_callback_route_specifier_remove_request_header(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key) {
  auto* context = settableContext(context_envoy_ptr);
  return recordHeaderRemoval(
      context, key,
      context != nullptr
          ? context->overrides.request_headers_to_remove
          : routeSpecifierContext(context_envoy_ptr)->overrides.request_headers_to_remove);
}

bool envoy_dynamic_module_callback_route_specifier_add_response_header(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value,
    envoy_dynamic_module_type_route_specifier_header_append_action action) {
  auto* context = settableContext(context_envoy_ptr);
  return recordHeaderMutation(
      context, key, value, action,
      context != nullptr
          ? context->overrides.response_headers_to_add
          : routeSpecifierContext(context_envoy_ptr)->overrides.response_headers_to_add);
}

bool envoy_dynamic_module_callback_route_specifier_remove_response_header(
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key) {
  auto* context = settableContext(context_envoy_ptr);
  return recordHeaderRemoval(
      context, key,
      context != nullptr
          ? context->overrides.response_headers_to_remove
          : routeSpecifierContext(context_envoy_ptr)->overrides.response_headers_to_remove);
}

} // extern "C"

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
