#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/router/cluster_specifiers/dynamic_modules/cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::HeadersMapOptConstRef;

namespace {

ClusterSpecifierContext*
clusterSpecifierContext(envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr ptr) {
  return static_cast<ClusterSpecifierContext*>(ptr);
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

Envoy::Stats::StatNameTagVector buildTagsForClusterSpecifierMetric(
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

// Returns a mutable handle to the route metadata value the module is setting, creating the
// namespace and key when they are absent.
Protobuf::Value& mutableRouteMetadataValue(ClusterSpecifierContext* context,
                                           envoy_dynamic_module_type_module_buffer ns,
                                           envoy_dynamic_module_type_module_buffer key) {
  return Envoy::Config::Metadata::mutableMetadataValue(context->selection.route_metadata,
                                                       std::string(ns.ptr, ns.length),
                                                       std::string(key.ptr, key.length));
}

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_cluster_specifier_get_request_headers_size(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr) {
  return clusterSpecifierContext(context_envoy_ptr)->headers.size();
}

bool envoy_dynamic_module_callback_cluster_specifier_get_request_headers(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getHeaders(HeadersMapOptConstRef(context->headers), result_headers);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_request_header_value(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getHeaderValue(HeadersMapOptConstRef(context->headers), key, result,
                                         index, total_count_out);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_attribute_string(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getAttributeString(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_attribute_int(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getAttributeInt(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_attribute_bool(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getAttributeBool(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getDynamicMetadata(context->stream_info, filter_name, path, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata_number(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, double* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getDynamicMetadataNumber(context->stream_info, filter_name, path, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata_bool(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, bool* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getDynamicMetadataBool(context->stream_info, filter_name, path, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_route_name(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const std::string& route_name = clusterSpecifierContext(context_envoy_ptr)->route_name;
  if (route_name.empty()) {
    return false;
  }
  *result = {route_name.data(), route_name.size()};
  return true;
}

uint64_t envoy_dynamic_module_callback_cluster_specifier_get_random_value(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr) {
  return clusterSpecifierContext(context_envoy_ptr)->random_value;
}

bool envoy_dynamic_module_callback_cluster_specifier_get_cluster_host_count(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name, uint32_t priority, size_t* total_count,
    size_t* healthy_count, size_t* degraded_count) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  auto* tl_cluster = context->config.clusterManager().getThreadLocalCluster(
      absl::string_view(cluster_name.ptr, cluster_name.length));
  if (tl_cluster == nullptr) {
    return false;
  }
  const auto& priority_set = tl_cluster->prioritySet();
  if (priority >= priority_set.hostSetsPerPriority().size()) {
    return false;
  }
  const auto& host_set = priority_set.hostSetsPerPriority()[priority];
  if (host_set == nullptr) {
    return false;
  }
  if (total_count != nullptr) {
    *total_count = host_set->hosts().size();
  }
  if (healthy_count != nullptr) {
    *healthy_count = host_set->healthyHosts().size();
  }
  if (degraded_count != nullptr) {
    *degraded_count = host_set->degradedHosts().size();
  }
  return true;
}

void envoy_dynamic_module_callback_cluster_specifier_set_cluster_name(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.cluster_name.assign(cluster_name.ptr, cluster_name.length);
}

void envoy_dynamic_module_callback_cluster_specifier_set_timeout(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t timeout_ms) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.timeout = toMilliseconds(timeout_ms);
}

void envoy_dynamic_module_callback_cluster_specifier_set_idle_timeout(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t idle_timeout_ms) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.idle_timeout = toMilliseconds(idle_timeout_ms);
}

void envoy_dynamic_module_callback_cluster_specifier_set_request_body_buffer_limit(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t limit_bytes) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.request_body_buffer_limit = limit_bytes;
}

void envoy_dynamic_module_callback_cluster_specifier_set_priority(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_resource_priority priority) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.priority = priority == envoy_dynamic_module_type_resource_priority_High
                                    ? Upstream::ResourcePriority::High
                                    : Upstream::ResourcePriority::Default;
}

bool envoy_dynamic_module_callback_cluster_specifier_set_cluster_not_found_response_code(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    uint32_t status_code) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  // The code is only used if the selected cluster turns out to be missing, so an invalid one would
  // otherwise surface as a malformed response far from this call. Reject it here instead.
  if (status_code < 200 || status_code >= 600) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module set the out of range cluster not found response code {}, so the response "
        "code of the matched route stays in effect",
        status_code);
    return false;
  }
  context->selection.cluster_not_found_response_code = static_cast<Http::Code>(status_code);
  return true;
}

bool envoy_dynamic_module_callback_cluster_specifier_set_route_action_override(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  const absl::string_view name_view(name.ptr, name.length);
  const RouteActionOverride* entry = context->config.routeActionOverride(name_view);
  if (entry == nullptr) {
    // The module and the route action overrides are configured separately, so a name that does not
    // resolve is usually a mismatch between the two rather than a deliberate probe. Rate limit the
    // warning because a mismatch repeats on every request.
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module selected the unknown route action override '{}', so the route action "
        "properties of the matched route stay in effect",
        name_view);
    return false;
  }
  context->selection.route_action_override = entry;
  return true;
}

void envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_number(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double value) {
  mutableRouteMetadataValue(clusterSpecifierContext(context_envoy_ptr), ns, key)
      .set_number_value(value);
}

void envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_string(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value) {
  mutableRouteMetadataValue(clusterSpecifierContext(context_envoy_ptr), ns, key)
      .set_string_value(std::string(value.ptr, value.length));
}

void envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_bool(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    bool value) {
  mutableRouteMetadataValue(clusterSpecifierContext(context_envoy_ptr), ns, key)
      .set_bool_value(value);
}

void envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_struct(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns,
    envoy_dynamic_module_type_module_buffer serialized_struct) {
  Protobuf::Struct metadata_value;
  if (!metadata_value.ParseFromArray(serialized_struct.ptr,
                                     static_cast<int>(serialized_struct.length))) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module set route metadata with a buffer that does not parse as a "
        "google.protobuf.Struct, so the call is ignored");
    return;
  }
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  (*context->selection.route_metadata.mutable_filter_metadata())[std::string(ns.ptr, ns.length)]
      .MergeFrom(metadata_value);
}

void envoy_dynamic_module_callback_cluster_specifier_set_route_typed_metadata(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns,
    envoy_dynamic_module_type_module_buffer serialized_any) {
  Protobuf::Any typed_value;
  if (!typed_value.ParseFromArray(serialized_any.ptr, static_cast<int>(serialized_any.length))) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module set typed route metadata with a buffer that does not parse as a "
        "google.protobuf.Any, so the call is ignored");
    return;
  }
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  // Assign rather than merge so the whole Any is swapped at once. Merging an Any field by field
  // could keep the type URL of one call with the value of another.
  (*context->selection.route_metadata
        .mutable_typed_filter_metadata())[std::string(ns.ptr, ns.length)] = typed_value;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_define_counter(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_.load(std::memory_order_acquire)) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);

  if (label_names_length == 0) {
    Envoy::Stats::Counter& counter =
        Envoy::Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
    *counter_id_ptr = config->addCounter({counter});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *counter_id_ptr = config->addCounterVec({main_stat_name, label_names_vec});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_increment_counter(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);

  if (label_values_length == 0) {
    auto counter = config->getCounterById(id);
    if (!counter.has_value()) {
      if (config->getCounterVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->stats_scope_->symbolTable());
  auto tags = buildTagsForClusterSpecifierMetric(dynamic_pool, counter->getLabelNames(),
                                                 label_values, label_values_length);
  counter->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_define_gauge(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_.load(std::memory_order_acquire)) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);
  Envoy::Stats::Gauge::ImportMode import_mode = Envoy::Stats::Gauge::ImportMode::Accumulate;

  if (label_names_length == 0) {
    Envoy::Stats::Gauge& gauge = Envoy::Stats::Utility::gaugeFromStatNames(
        *config->stats_scope_, {main_stat_name}, import_mode);
    *gauge_id_ptr = config->addGauge({gauge});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *gauge_id_ptr = config->addGaugeVec({main_stat_name, label_names_vec, import_mode});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_set_gauge(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->stats_scope_->symbolTable());
  auto tags = buildTagsForClusterSpecifierMetric(dynamic_pool, gauge->getLabelNames(), label_values,
                                                 label_values_length);
  gauge->set(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->stats_scope_->symbolTable());
  auto tags = buildTagsForClusterSpecifierMetric(dynamic_pool, gauge->getLabelNames(), label_values,
                                                 label_values_length);
  gauge->add(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);

  if (label_values_length == 0) {
    auto gauge = config->getGaugeById(id);
    if (!gauge.has_value()) {
      if (config->getGaugeVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->sub(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  auto gauge = config->getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->stats_scope_->symbolTable());
  auto tags = buildTagsForClusterSpecifierMetric(dynamic_pool, gauge->getLabelNames(), label_values,
                                                 label_values_length);
  gauge->sub(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_define_histogram(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_.load(std::memory_order_acquire)) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  Envoy::Stats::StatName main_stat_name = config->stat_name_pool_.add(name_view);
  Envoy::Stats::Histogram::Unit unit = Envoy::Stats::Histogram::Unit::Unspecified;

  if (label_names_length == 0) {
    Envoy::Stats::Histogram& histogram = Envoy::Stats::Utility::histogramFromStatNames(
        *config->stats_scope_, {main_stat_name}, unit);
    *histogram_id_ptr = config->addHistogram({histogram});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Envoy::Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(config->stat_name_pool_.add(label_name_view));
  }
  *histogram_id_ptr = config->addHistogramVec({main_stat_name, label_names_vec, unit});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value(
    envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleClusterSpecifierConfig*>(config_envoy_ptr);

  if (label_values_length == 0) {
    auto histogram = config->getHistogramById(id);
    if (!histogram.has_value()) {
      if (config->getHistogramVecById(id).has_value()) {
        return envoy_dynamic_module_type_metrics_result_InvalidLabels;
      }
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
  Envoy::Stats::StatNameDynamicPool dynamic_pool(config->stats_scope_->symbolTable());
  auto tags = buildTagsForClusterSpecifierMetric(dynamic_pool, histogram->getLabelNames(),
                                                 label_values, label_values_length);
  histogram->recordValue(*config->stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
