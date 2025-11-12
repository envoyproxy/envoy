#include <cstddef>

#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {
namespace {

void bodyBufferToModule(const Buffer::Instance& buffer,
                        envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto raw_slices = buffer.getRawSlices(std::nullopt);
  auto counter = 0;
  for (const auto& slice : raw_slices) {
    result_buffer_vector[counter].length = slice.len_;
    result_buffer_vector[counter].ptr = static_cast<char*>(slice.mem_);
    counter++;
  }
}

} // namespace

static Stats::StatNameTagVector
buildTagsForModuleMetric(DynamicModuleHttpFilter& filter, const Stats::StatNameVec& label_names,
                         envoy_dynamic_module_type_module_buffer* label_values,
                         size_t label_values_length) {

  ASSERT(label_values_length == label_names.size());
  Stats::StatNameTagVector tags;
  tags.reserve(label_values_length);
  for (size_t i = 0; i < label_values_length; i++) {
    absl::string_view label_value_view(label_values[i].ptr, label_values[i].length);
    auto label_value = filter.getStatNamePool().add(label_value_view);
    tags.push_back(Stats::StatNameTag(label_names[i], label_value));
  }
  return tags;
}

extern "C" {

using HeadersMapOptConstRef = OptRef<const Http::HeaderMap>;
using HeadersMapOptRef = OptRef<Http::HeaderMap>;

bool getHeaderValueImpl(HeadersMapOptConstRef map, envoy_dynamic_module_type_buffer_module_ptr key,
                        size_t key_length,
                        envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
                        size_t* result_buffer_length_ptr, size_t index, size_t* optional_size) {
  if (!map.has_value()) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    if (optional_size != nullptr) {
      *optional_size = 0;
    }
    return false;
  }
  absl::string_view key_view(key, key_length);

  // TODO: we might want to avoid copying the key here by trusting the key is already lower case.
  const auto values = map->get(Envoy::Http::LowerCaseString(key_view));
  if (optional_size != nullptr) {
    *optional_size = values.size();
  }

  if (index >= values.size()) {
    *result_buffer_ptr = nullptr;
    *result_buffer_length_ptr = 0;
    return false;
  }

  const auto value = values[index]->value().getStringView();
  *result_buffer_ptr = const_cast<char*>(value.data());
  *result_buffer_length_ptr = value.size();
  return true;
}

bool headerAsAttribute(HeadersMapOptConstRef map, const Envoy::Http::LowerCaseString& header,
                       envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
                       size_t* result_buffer_length_ptr) {
  if (!map.has_value()) {
    return false;
  }
  auto lower_header = header.get();
  envoy_dynamic_module_type_buffer_envoy_ptr key_ptr = const_cast<char*>(lower_header.data());
  return getHeaderValueImpl(map, key_ptr, lower_header.size(), result_buffer_ptr,
                            result_buffer_length_ptr, 0, nullptr);
}

bool getSslInfo(
    OptRef<const Network::Connection> connection,
    std::function<OptRef<const std::string>(const Ssl::ConnectionInfoConstSharedPtr)> get_san_func,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr,
    size_t* result_buffer_length_ptr) {
  if (!connection.has_value() || !connection->ssl()) {
    return false;
  }
  const Ssl::ConnectionInfoConstSharedPtr ssl = connection->ssl();
  OptRef<const std::string> ssl_attribute = get_san_func(ssl);
  if (!ssl_attribute.has_value()) {
    return false;
  }
  const std::string& attribute = ssl_attribute.value();
  *result_buffer_ptr = const_cast<char*>(attribute.data());
  *result_buffer_length_ptr = attribute.size();
  return true;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_counter(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length, size_t* counter_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name, name_length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::Counter& c =
      Stats::Utility::counterFromStatNames(*filter_config->stats_scope_, {main_stat_name});
  *counter_id_ptr = filter_config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_counter_vec(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name, name_length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(filter_config->stat_name_pool_.add(label_name_view));
  }
  *counter_id_ptr = filter_config->addCounterVec({main_stat_name, label_names_vec});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_increment_counter(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto counter = filter->getFilterConfig().getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_increment_counter_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto counter = filter->getFilterConfig().getCounterVecById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != counter->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags = buildTagsForModuleMetric(*filter, counter->getLabelNames(), label_values,
                                       label_values_length);
  counter->add(*filter->getFilterConfig().stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_gauge(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length, size_t* gauge_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name, name_length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::Gauge::ImportMode import_mode =
      Stats::Gauge::ImportMode::Accumulate; // TODO: make this configurable?
  Stats::Gauge& g = Stats::Utility::gaugeFromStatNames(*filter_config->stats_scope_,
                                                       {main_stat_name}, import_mode);
  *gauge_id_ptr = filter_config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_gauge_vec(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name, name_length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::Gauge::ImportMode import_mode =
      Stats::Gauge::ImportMode::Accumulate; // TODO: make this configurable?
  Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(filter_config->stat_name_pool_.add(label_name_view));
  }
  *gauge_id_ptr = filter_config->addGaugeVec({main_stat_name, label_names_vec, import_mode});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_increase_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->increase(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_increase_gauge_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForModuleMetric(*filter, gauge->getLabelNames(), label_values, label_values_length);
  gauge->increase(*filter->getFilterConfig().stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_decrease_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->decrease(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_decrease_gauge_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForModuleMetric(*filter, gauge->getLabelNames(), label_values, label_values_length);
  gauge->decrease(*filter->getFilterConfig().stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_set_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_set_gauge_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeVecById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != gauge->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForModuleMetric(*filter, gauge->getLabelNames(), label_values, label_values_length);
  gauge->set(*filter->getFilterConfig().stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_histogram(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    size_t* histogram_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name, name_length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::Histogram::Unit unit =
      Stats::Histogram::Unit::Unspecified; // TODO: make this configurable?
  Stats::Histogram& h =
      Stats::Utility::histogramFromStatNames(*filter_config->stats_scope_, {main_stat_name}, unit);
  *histogram_id_ptr = filter_config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_histogram_vec(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name, name_length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::Histogram::Unit unit =
      Stats::Histogram::Unit::Unspecified; // TODO: make this configurable?
  Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(filter_config->stat_name_pool_.add(label_name_view));
  }
  *histogram_id_ptr = filter_config->addHistogramVec({main_stat_name, label_names_vec, unit});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_record_histogram_value(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto hist = filter->getFilterConfig().getHistogramById(id);
  if (!hist.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  hist->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_record_histogram_value_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto hist = filter->getFilterConfig().getHistogramVecById(id);
  if (!hist.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  if (label_values_length != hist->getLabelNames().size()) {
    return envoy_dynamic_module_type_metrics_result_InvalidLabels;
  }
  auto tags =
      buildTagsForModuleMetric(*filter, hist->getLabelNames(), label_values, label_values_length);
  hist->recordValue(*filter->getFilterConfig().stats_scope_, tags, value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

bool envoy_dynamic_module_callback_http_get_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->requestHeaders(), key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index, optional_size);
}

bool envoy_dynamic_module_callback_http_get_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->requestTrailers(), key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index, optional_size);
}

bool envoy_dynamic_module_callback_http_get_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->responseHeaders(), key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index, optional_size);
}

bool envoy_dynamic_module_callback_http_get_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(filter->responseTrailers(), key, key_length, result_buffer_ptr,
                            result_buffer_length_ptr, index, optional_size);
}

bool addHeaderValueImpl(HeadersMapOptRef map, envoy_dynamic_module_type_buffer_module_ptr key,
                        size_t key_length, envoy_dynamic_module_type_buffer_module_ptr value,
                        size_t value_length) {
  if (!map.has_value()) {
    return false;
  }
  if (value == nullptr) {
    return false;
  }
  absl::string_view key_view(key, key_length);
  absl::string_view value_view(value, value_length);
  map->addCopy(Envoy::Http::LowerCaseString(key_view), value_view);
  return true;
}

bool envoy_dynamic_module_callback_http_add_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return addHeaderValueImpl(filter->requestHeaders(), key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_add_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return addHeaderValueImpl(filter->requestTrailers(), key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_add_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return addHeaderValueImpl(filter->responseHeaders(), key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_add_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return addHeaderValueImpl(filter->responseTrailers(), key, key_length, value, value_length);
}

bool setHeaderValueImpl(HeadersMapOptRef map, envoy_dynamic_module_type_buffer_module_ptr key,
                        size_t key_length, envoy_dynamic_module_type_buffer_module_ptr value,
                        size_t value_length) {
  if (!map.has_value()) {
    return false;
  }
  absl::string_view key_view(key, key_length);
  if (value == nullptr) {
    map->remove(Envoy::Http::LowerCaseString(key_view));
    return true;
  }
  absl::string_view value_view(value, value_length);
  // TODO: we might want to avoid copying the key here by trusting the key is already lower case.
  map->setCopy(Envoy::Http::LowerCaseString(key_view), value_view);
  return true;
}

bool envoy_dynamic_module_callback_http_set_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->requestHeaders(), key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_set_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->requestTrailers(), key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_set_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->responseHeaders(), key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_set_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(filter->responseTrailers(), key, key_length, value, value_length);
}

bool envoy_dynamic_module_callback_http_get_request_headers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  RequestHeaderMapOptRef request_headers = filter->requestHeaders();
  if (!request_headers.has_value()) {
    *size = 0;
    return false;
  }
  *size = request_headers->size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_request_trailers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  RequestTrailerMapOptRef request_trailers = filter->requestTrailers();
  if (!request_trailers.has_value()) {
    *size = 0;
    return false;
  }
  *size = request_trailers->size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_response_headers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  ResponseHeaderMapOptRef response_headers = filter->responseHeaders();
  if (!response_headers.has_value()) {
    *size = 0;
    return false;
  }
  *size = response_headers->size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_response_trailers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  ResponseTrailerMapOptRef response_trailers = filter->responseTrailers();
  if (!response_trailers.has_value()) {
    *size = 0;
    return false;
  }
  *size = response_trailers->size();
  return true;
}

bool getHeadersImpl(HeadersMapOptConstRef map,
                    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  if (!map) {
    return false;
  }
  size_t i = 0;
  map->iterate([&i, &result_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

bool envoy_dynamic_module_callback_http_get_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->requestHeaders(), result_headers);
}

bool envoy_dynamic_module_callback_http_get_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->requestTrailers(), result_headers);
}

bool envoy_dynamic_module_callback_http_get_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->responseHeaders(), result_headers);
}

bool envoy_dynamic_module_callback_http_get_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(filter->responseTrailers(), result_headers);
}

void envoy_dynamic_module_callback_http_send_response(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t status_code,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    envoy_dynamic_module_type_buffer_module_ptr body_ptr, size_t body_length) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  std::function<void(ResponseHeaderMap & headers)> modify_headers = nullptr;
  if (headers_vector != nullptr && headers_vector_size != 0) {
    modify_headers = [headers_vector, headers_vector_size](ResponseHeaderMap& headers) {
      for (size_t i = 0; i < headers_vector_size; i++) {
        const auto& header = &headers_vector[i];
        const absl::string_view key(static_cast<const char*>(header->key_ptr), header->key_length);
        const absl::string_view value(static_cast<const char*>(header->value_ptr),
                                      header->value_length);
        headers.addCopy(Http::LowerCaseString(key), value);
      }
    };
  }
  const absl::string_view body =
      body_ptr ? absl::string_view(static_cast<const char*>(body_ptr), body_length) : "";

  filter->sendLocalReply(static_cast<Http::Code>(status_code), body, modify_headers, 0,
                         "dynamic_module");
}

void envoy_dynamic_module_callback_http_send_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    bool end_stream) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  std::unique_ptr<ResponseHeaderMapImpl> headers = ResponseHeaderMapImpl::create();
  for (size_t i = 0; i < headers_vector_size; i++) {
    const auto& header = &headers_vector[i];
    const absl::string_view key(static_cast<const char*>(header->key_ptr), header->key_length);
    const absl::string_view value(static_cast<const char*>(header->value_ptr),
                                  header->value_length);
    headers->addCopy(Http::LowerCaseString(key), value);
  }

  filter->decoder_callbacks_->encodeHeaders(std::move(headers), end_stream, "");
}

void envoy_dynamic_module_callback_http_send_response_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length, bool end_stream) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  Buffer::OwnedImpl buffer(static_cast<const char*>(data), length);
  filter->decoder_callbacks_->encodeData(buffer, end_stream);
}

void envoy_dynamic_module_callback_http_send_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* trailers_vector, size_t trailers_vector_size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  std::unique_ptr<ResponseTrailerMapImpl> trailers = ResponseTrailerMapImpl::create();
  for (size_t i = 0; i < trailers_vector_size; i++) {
    const auto& trailer = &trailers_vector[i];
    const absl::string_view key(static_cast<const char*>(trailer->key_ptr), trailer->key_length);
    const absl::string_view value(static_cast<const char*>(trailer->value_ptr),
                                  trailer->value_length);
    trailers->addCopy(Http::LowerCaseString(key), value);
  }

  filter->decoder_callbacks_->encodeTrailers(std::move(trailers));
}

/**
 * Helper to get the metadata namespace from the metadata.
 * @param metadata is the metadata to search in.
 * @param namespace_ptr is the namespace of the metadata.
 * @param namespace_length is the length of the namespace.
 * @return the metadata namespace if it exists, nullptr otherwise.
 *
 * This will be reused by all envoy_dynamic_module_type_metadata_source where
 * each variant differs in the returned type of the metadata. For example, route metadata will
 * return OptRef vs upstream host metadata will return a shared pointer.
 */
const Protobuf::Struct*
getMetadataNamespaceImpl(const envoy::config::core::v3::Metadata& metadata,
                         envoy_dynamic_module_type_buffer_module_ptr namespace_ptr,
                         size_t namespace_length) {
  absl::string_view namespace_view(static_cast<const char*>(namespace_ptr), namespace_length);
  auto metadata_namespace = metadata.filter_metadata().find(namespace_view);
  if (metadata_namespace == metadata.filter_metadata().end()) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("namespace {} not found in metadata", namespace_view));
    return nullptr;
  }
  return &metadata_namespace->second;
}

/**
 * Helper to get the metadata namespace from the stream info.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source the location of the metadata to use.
 * @param namespace_ptr is the namespace of the metadata.
 * @param namespace_length is the length of the namespace.
 * @return the metadata namespace if it exists, nullptr otherwise.
 */
const Protobuf::Struct*
getMetadataNamespace(envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
                     envoy_dynamic_module_type_metadata_source metadata_source,
                     envoy_dynamic_module_type_buffer_module_ptr namespace_ptr,
                     size_t namespace_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (!callbacks) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "callbacks are not available");
    return nullptr;
  }
  auto& stream_info = callbacks->streamInfo();

  switch (metadata_source) {
  case envoy_dynamic_module_type_metadata_source_Dynamic: {
    return getMetadataNamespaceImpl(stream_info.dynamicMetadata(), namespace_ptr, namespace_length);
  }
  case envoy_dynamic_module_type_metadata_source_Route: {
    auto route = stream_info.route();
    if (route) {
      return getMetadataNamespaceImpl(route->metadata(), namespace_ptr, namespace_length);
    }
    break;
  }
  case envoy_dynamic_module_type_metadata_source_Cluster: {
    auto clusterInfo = callbacks->clusterInfo();
    if (clusterInfo) {
      return getMetadataNamespaceImpl(clusterInfo->metadata(), namespace_ptr, namespace_length);
    }
    break;
  }
  case envoy_dynamic_module_type_metadata_source_Host: {
    std::shared_ptr<StreamInfo::UpstreamInfo> upstreamInfo = stream_info.upstreamInfo();
    if (upstreamInfo) {
      Upstream::HostDescriptionConstSharedPtr hostInfo = upstreamInfo->upstreamHost();
      if (hostInfo) {
        Upstream::MetadataConstSharedPtr md = hostInfo->metadata();
        if (md) {
          return getMetadataNamespaceImpl(*md, namespace_ptr, namespace_length);
        }
      }
    }
    break;
  }
  case envoy_dynamic_module_type_metadata_source_HostLocality: {
    std::shared_ptr<StreamInfo::UpstreamInfo> upstreamInfo = stream_info.upstreamInfo();
    if (upstreamInfo) {
      Upstream::HostDescriptionConstSharedPtr hostInfo = upstreamInfo->upstreamHost();
      if (hostInfo) {
        Upstream::MetadataConstSharedPtr md = hostInfo->localityMetadata();
        if (md) {
          return getMetadataNamespaceImpl(*md, namespace_ptr, namespace_length);
        }
      }
    }
    break;
  }
  }
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                      "metadata is not available");
  return nullptr;
}

/**
 * Helper to get the dynamic metadata namespace from the stream info. if the namespace does not
 * exist, it will be create, assuming stream info is available.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @return the metadata namespace if it exists, nullptr otherwise.
 */
Protobuf::Struct*
getDynamicMetadataNamespace(envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
                            envoy_dynamic_module_type_buffer_module_ptr namespace_ptr,
                            size_t namespace_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return nullptr;
  }
  auto metadata = stream_info->dynamicMetadata().mutable_filter_metadata();
  absl::string_view namespace_view(static_cast<const char*>(namespace_ptr), namespace_length);
  auto metadata_namespace = metadata->find(namespace_view);
  if (metadata_namespace == metadata->end()) {
    metadata_namespace = metadata->emplace(namespace_view, Protobuf::Struct{}).first;
  }
  return &metadata_namespace->second;
}

/**
 * Helper to get the metadata value from the metadata namespace.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @param key_ptr is the key of the dynamic metadata.
 * @param key_length is the length of the key.
 * @return the metadata value if it exists, nullptr otherwise.
 */
const Protobuf::Value*
getMetadataValue(envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
                 envoy_dynamic_module_type_metadata_source metadata_source,
                 envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
                 envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length) {
  auto metadata_namespace =
      getMetadataNamespace(filter_envoy_ptr, metadata_source, namespace_ptr, namespace_length);
  if (!metadata_namespace) {
    return nullptr;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  auto key_metadata = metadata_namespace->fields().find(key_view);
  if (key_metadata == metadata_namespace->fields().end()) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("key {} not found in metadata namespace", key_view));
    return nullptr;
  }
  return &key_metadata->second;
}

bool envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double value) {
  auto metadata_namespace =
      getDynamicMetadataNamespace(filter_envoy_ptr, namespace_ptr, namespace_length);
  if (!metadata_namespace) {
    // If stream info is not available, we cannot guarantee that the namespace is created.
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  Protobuf::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_number_value(value);
  metadata_namespace->MergeFrom(metadata_value);
  return true;
}

bool envoy_dynamic_module_callback_http_get_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double* result) {
  const auto key_metadata = getMetadataValue(filter_envoy_ptr, metadata_source, namespace_ptr,
                                             namespace_length, key_ptr, key_length);
  if (!key_metadata) {
    return false;
  }
  if (!key_metadata->has_number_value()) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
        fmt::format("key {} is not a number",
                    absl::string_view(static_cast<const char*>(key_ptr), key_length)));
    return false;
  }
  *result = key_metadata->number_value();
  return true;
}

bool envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length) {
  auto metadata_namespace =
      getDynamicMetadataNamespace(filter_envoy_ptr, namespace_ptr, namespace_length);
  if (!metadata_namespace) {
    // If stream info is not available, we cannot guarantee that the namespace is created.
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  absl::string_view value_view(static_cast<const char*>(value_ptr), value_length);
  Protobuf::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_string_value(value_view);
  metadata_namespace->MergeFrom(metadata_value);
  return true;
}

bool envoy_dynamic_module_callback_http_get_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length) {
  const auto key_metadata = getMetadataValue(filter_envoy_ptr, metadata_source, namespace_ptr,
                                             namespace_length, key_ptr, key_length);
  if (!key_metadata) {
    return false;
  }
  if (!key_metadata->has_string_value()) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
        fmt::format("key {} is not a string",
                    absl::string_view(static_cast<const char*>(key_ptr), key_length)));
    return false;
  }
  const auto& value = key_metadata->string_value();
  *result = const_cast<char*>(value.data());
  *result_length = value.size();
  return true;
}

bool envoy_dynamic_module_callback_http_set_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  absl::string_view value_view(static_cast<const char*>(value_ptr), value_length);
  stream_info->filterState()->setData(key_view,
                                      std::make_unique<Router::StringAccessorImpl>(value_view),
                                      StreamInfo::FilterState::StateType::ReadOnly);
  return true;
}

bool envoy_dynamic_module_callback_http_get_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return false;
  }
  absl::string_view key_view(static_cast<const char*>(key_ptr), key_length);
  auto filter_state = stream_info->filterState()->getDataReadOnly<Router::StringAccessor>(key_view);
  if (!filter_state) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("key not found in filter state", key_view));
    return false;
  }
  absl::string_view str = filter_state->asString();
  *result = const_cast<char*>(str.data());
  *result_length = str.size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_received_request_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_request_body_;
  if (!buffer) {
    return false;
  }
  // See the comment on current_request_body_ for when we reach this line.
  *size = buffer->length();
  return true;
}

bool envoy_dynamic_module_callback_http_get_buffered_request_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->decoder_callbacks_->decodingBuffer();
  if (!buffer) {
    return false;
  }
  *size = buffer->length();
  return true;
}

bool envoy_dynamic_module_callback_http_get_received_response_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_response_body_;
  if (!buffer) {
    return false;
  }
  // See the comment on current_response_body_ for when we reach this line.
  *size = buffer->length();
  return true;
}

bool envoy_dynamic_module_callback_http_get_buffered_response_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->encoder_callbacks_->encodingBuffer();
  if (!buffer) {
    return false;
  }
  *size = buffer->length();
  return true;
}

bool envoy_dynamic_module_callback_http_get_received_request_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_request_body_;
  if (!buffer) {
    return false;
  }
  // See the comment on current_request_body_ for when we reach this.
  bodyBufferToModule(*buffer, result_buffer_vector);
  return true;
}

bool envoy_dynamic_module_callback_http_get_buffered_request_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->decoder_callbacks_->decodingBuffer();
  if (!buffer) {
    return false;
  }
  bodyBufferToModule(*buffer, result_buffer_vector);
  return true;
}

bool envoy_dynamic_module_callback_http_get_received_request_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_request_body_;
  if (!buffer) {
    return false;
  }
  // See the comment on current_request_body_ for when we reach this line.
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_buffered_request_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->decoder_callbacks_->decodingBuffer();
  if (!buffer) {
    return false;
  }
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

bool envoy_dynamic_module_callback_http_append_received_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_request_body_;
  if (!buffer) {
    return false;
  }
  buffer->add(absl::string_view(static_cast<const char*>(data), length));
  return true;
}

bool envoy_dynamic_module_callback_http_append_buffered_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->decoder_callbacks_->decodingBuffer();
  if (!buffer) {
    Buffer::OwnedImpl buffer;
    buffer.add(data, length);
    filter->decoder_callbacks_->addDecodedData(buffer, true);
  } else {
    filter->decoder_callbacks_->modifyDecodingBuffer([data, length](Buffer::Instance& buffer) {
      buffer.add(absl::string_view(static_cast<const char*>(data), length));
    });
  }
  return true;
}

bool envoy_dynamic_module_callback_http_drain_received_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_request_body_;
  if (!buffer) {
    return false;
  }
  auto size = std::min<uint64_t>(filter->current_request_body_->length(), number_of_bytes);
  filter->current_request_body_->drain(size);
  return true;
}

bool envoy_dynamic_module_callback_http_drain_buffered_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->decoder_callbacks_->decodingBuffer();
  if (!buffer) {
    return false;
  }
  filter->decoder_callbacks_->modifyDecodingBuffer([number_of_bytes](Buffer::Instance& buffer) {
    auto size = std::min<uint64_t>(buffer.length(), number_of_bytes);
    buffer.drain(size);
  });
  return true;
}

bool envoy_dynamic_module_callback_http_get_received_response_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_response_body_;
  if (!buffer) {
    return false;
  }
  // See the comment on current_response_body_ for when we reach this.
  bodyBufferToModule(*buffer, result_buffer_vector);
  return true;
}

bool envoy_dynamic_module_callback_http_get_buffered_response_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->encoder_callbacks_->encodingBuffer();
  if (!buffer) {
    return false;
  }
  bodyBufferToModule(*buffer, result_buffer_vector);
  return true;
}

bool envoy_dynamic_module_callback_http_get_received_response_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_response_body_;
  if (!buffer) {
    return false;
  }
  // See the comment on current_response_body_ for when we reach this line.
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_buffered_response_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->encoder_callbacks_->encodingBuffer();
  if (!buffer) {
    return false;
  }
  *size = buffer->getRawSlices(std::nullopt).size();
  return true;
}

bool envoy_dynamic_module_callback_http_append_received_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_response_body_;
  if (!buffer) {
    return false;
  }
  buffer->add(absl::string_view(static_cast<const char*>(data), length));
  return true;
}

bool envoy_dynamic_module_callback_http_append_buffered_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->encoder_callbacks_->encodingBuffer();
  if (!buffer) {
    Buffer::OwnedImpl buffer;
    buffer.add(data, length);
    filter->encoder_callbacks_->addEncodedData(buffer, true);
  } else {
    filter->encoder_callbacks_->modifyEncodingBuffer([data, length](Buffer::Instance& buffer) {
      buffer.add(absl::string_view(static_cast<const char*>(data), length));
    });
  }
  return true;
}

bool envoy_dynamic_module_callback_http_drain_received_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->current_response_body_;
  if (!buffer) {
    return false;
  }
  auto size = std::min<uint64_t>(filter->current_response_body_->length(), number_of_bytes);
  filter->current_response_body_->drain(size);
  return true;
}

bool envoy_dynamic_module_callback_http_drain_buffered_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = filter->encoder_callbacks_->encodingBuffer();
  if (!buffer) {
    return false;
  }
  filter->encoder_callbacks_->modifyEncodingBuffer([number_of_bytes](Buffer::Instance& buffer) {
    auto size = std::min<uint64_t>(buffer.length(), number_of_bytes);
    buffer.drain(size);
  });
  return true;
}

void envoy_dynamic_module_callback_http_clear_route_cache(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  filter->decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
}

envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_callback_get_most_specific_route_config(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  const auto* config =
      Http::Utility::resolveMostSpecificPerFilterConfig<DynamicModuleHttpPerRouteFilterConfig>(
          filter->decoder_callbacks_);
  if (!config) {
    return nullptr;
  }
  return config->config_;
}

bool envoy_dynamic_module_callback_http_filter_get_attribute_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_RequestProtocol: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto protocol = stream_info->protocol();
      if (protocol.has_value()) {
        const auto& protocol_string_ref = Http::Utility::getProtocolString(protocol.value());
        *result = const_cast<char*>(protocol_string_ref.data());
        *result_length = protocol_string_ref.size();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamAddress: {
    const auto upstream_info = filter->upstreamInfo();
    if (upstream_info) {
      auto upstream_host = upstream_info->upstreamHost();
      if (upstream_host != nullptr && upstream_host->address() != nullptr) {
        auto addr = upstream_host->address()->asStringView();
        *result = const_cast<char*>(addr.data());
        *result_length = addr.size();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourceAddress: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto addressProvider =
          stream_info->downstreamAddressProvider().remoteAddress()->asStringView();
      *result = const_cast<char*>(addressProvider.data());
      *result_length = addressProvider.size();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationAddress: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto addressProvider =
          stream_info->downstreamAddressProvider().localAddress()->asStringView();
      *result = const_cast<char*>(addressProvider.data());
      *result_length = addressProvider.size();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestId: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      auto stream_id_provider = stream_info->getStreamIdProvider();
      if (stream_id_provider.has_value()) {
        const absl::optional<absl::string_view> request_id = stream_id_provider->toStringView();
        if (request_id.has_value()) {
          *result = const_cast<char*>(request_id->data());
          *result_length = request_id->size();
          ok = true;
        }
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestPath: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Path, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestHost: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Host, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestMethod: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Method, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestScheme: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Scheme, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestReferer: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::CustomHeaders::get().Referer,
                           result, result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestUserAgent: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().UserAgent, result,
                           result_length);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestUrlPath: {
    RequestHeaderMapOptRef headers = filter->requestHeaders();
    if (headers.has_value()) {
      const absl::string_view path = headers->getPathValue();
      size_t query_offset = path.find('?');
      if (query_offset == absl::string_view::npos) {
        *result = const_cast<char*>(path.data());
        *result_length = path.size();
      } else {
        const auto url_path = path.substr(0, query_offset);
        *result = const_cast<char*>(url_path.data());
        *result_length = url_path.length();
      }
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestQuery: {
    RequestHeaderMapOptRef headers = filter->requestHeaders();
    if (headers.has_value()) {
      const absl::string_view path = headers->getPathValue();
      size_t query_offset = path.find('?');
      if (query_offset != absl::string_view::npos) {
        auto query = path.substr(query_offset + 1);
        const auto fragment_offset = query.find('#');
        query = query.substr(0, fragment_offset);
        *result = const_cast<char*>(query.data());
        *result_length = query.length();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_XdsRouteName: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto& route_name = stream_info->getRouteName();
      *result = const_cast<char*>(route_name.data());
      *result_length = route_name.size();
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->tlsVersion();
        },
        result, result_length);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectLocalCertificate();
        },
        result, result_length);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectPeerCertificate();
        },
        result, result_length);
  case envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->sha256PeerCertificateDigest();
        },
        result, result_length);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanLocalCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansLocalCertificate().front();
        },
        result, result_length);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanPeerCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansPeerCertificate().front();
        },
        result, result_length);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanLocalCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanLocalCertificate().front();
        },
        result, result_length);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanPeerCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanPeerCertificate().front();
        },
        result, result_length);
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
                        "Unsupported attribute ID {} as string",
                        static_cast<int64_t>(attribute_id));
    break;
  }
  return ok;
}

bool envoy_dynamic_module_callback_http_filter_get_attribute_int(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_ResponseCode: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto code = stream_info->responseCode();
      if (code.has_value()) {
        *result = code.value();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_UpstreamPort: {
    const auto upstream_info = filter->upstreamInfo();
    if (upstream_info) {
      auto upstream_host = upstream_info->upstreamHost();
      if (upstream_host != nullptr && upstream_host->address() != nullptr) {
        auto ip = upstream_host->address()->ip();
        if (ip) {
          *result = ip->port();
          ok = true;
        }
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_SourcePort: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto ip = stream_info->downstreamAddressProvider().remoteAddress()->ip();
      if (ip) {
        *result = ip->port();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationPort: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto ip = stream_info->downstreamAddressProvider().localAddress()->ip();
      if (ip) {
        *result = ip->port();
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_ConnectionId: {
    const auto connection = filter->connection();
    if (connection) {
      *result = connection->id();
      ok = true;
    }
    break;
  }
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
                        "Unsupported attribute ID {} as int", static_cast<int64_t>(attribute_id));
  }
  return ok;
}

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_http_callout(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t callout_id,
    envoy_dynamic_module_type_buffer_module_ptr cluster_name, size_t cluster_name_length,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_buffer_module_ptr body, size_t body_size,
    uint64_t timeout_milliseconds) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  // Try to get the cluster from the cluster manager for the given cluster name.
  absl::string_view cluster_name_view(cluster_name, cluster_name_length);

  // Construct the request message, starting with the headers, checking for required headers, and
  // adding the body if present.
  std::unique_ptr<RequestHeaderMapImpl> hdrs = Http::RequestHeaderMapImpl::create();
  for (size_t i = 0; i < headers_size; i++) {
    const auto& header = &headers[i];
    const absl::string_view key(static_cast<const char*>(header->key_ptr), header->key_length);
    const absl::string_view value(static_cast<const char*>(header->value_ptr),
                                  header->value_length);
    hdrs->addCopy(Http::LowerCaseString(key), value);
  }
  Http::RequestMessagePtr message(new Http::RequestMessageImpl(std::move(hdrs)));
  if (message->headers().Path() == nullptr || message->headers().Method() == nullptr ||
      message->headers().Host() == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders;
  }
  if (body_size > 0) {
    message->body().add(absl::string_view(static_cast<const char*>(body), body_size));
    message->headers().setContentLength(body_size);
  }
  return filter->sendHttpCallout(callout_id, cluster_name_view, std::move(message),
                                 timeout_milliseconds);
}

envoy_dynamic_module_type_http_filter_scheduler_module_ptr
envoy_dynamic_module_callback_http_filter_scheduler_new(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return new DynamicModuleHttpFilterScheduler(filter->weak_from_this(),
                                              filter->decoder_callbacks_->dispatcher());
}

void envoy_dynamic_module_callback_http_filter_scheduler_delete(
    envoy_dynamic_module_type_http_filter_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleHttpFilterScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_http_filter_scheduler_commit(
    envoy_dynamic_module_type_http_filter_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  DynamicModuleHttpFilterScheduler* scheduler =
      static_cast<DynamicModuleHttpFilterScheduler*>(scheduler_module_ptr);
  scheduler->commit(event_id);
}

void envoy_dynamic_module_callback_http_filter_continue_decoding(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  filter->continueDecoding();
}

void envoy_dynamic_module_callback_http_filter_continue_encoding(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  filter->continueEncoding();
}

bool envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level level) {
  return Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules).level() <=
         static_cast<spdlog::level::level_enum>(level);
}

void envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level level,
                                       const char* message_ptr, size_t message_length) {
  absl::string_view message_view(static_cast<const char*>(message_ptr), message_length);
  spdlog::logger& logger = Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules);
  switch (level) {
  case envoy_dynamic_module_type_log_level_Debug:
    ENVOY_LOG_TO_LOGGER(logger, debug, message_view);
    break;
  case envoy_dynamic_module_type_log_level_Info:
    ENVOY_LOG_TO_LOGGER(logger, info, message_view);
    break;
  case envoy_dynamic_module_type_log_level_Warn:
    ENVOY_LOG_TO_LOGGER(logger, warn, message_view);
    break;
  case envoy_dynamic_module_type_log_level_Error:
    ENVOY_LOG_TO_LOGGER(logger, error, message_view);
    break;
  case envoy_dynamic_module_type_log_level_Critical:
    ENVOY_LOG_TO_LOGGER(logger, critical, message_view);
    break;
  default:
    break;
  }
}
}
} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
