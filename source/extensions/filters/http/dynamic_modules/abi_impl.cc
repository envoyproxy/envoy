#include <chrono>
#include <cstddef>
#include <cstdint>

#include "envoy/config/core/v3/socket_option.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/common/tracing/tracer_impl.h"
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

using HeadersMapOptConstRef = OptRef<const Http::HeaderMap>;
using HeadersMapOptRef = OptRef<Http::HeaderMap>;

HeadersMapOptRef getHeaderMapByType(DynamicModuleHttpFilter* filter,
                                    envoy_dynamic_module_type_http_header_type header_type) {
  switch (header_type) {
  case envoy_dynamic_module_type_http_header_type_RequestHeader:
    return filter->requestHeaders();
  case envoy_dynamic_module_type_http_header_type_RequestTrailer:
    return filter->requestTrailers();
  case envoy_dynamic_module_type_http_header_type_ResponseHeader:
    return filter->responseHeaders();
  case envoy_dynamic_module_type_http_header_type_ResponseTrailer:
    return filter->responseTrailers();
  default:
    return {};
  }
}

bool getHeaderValueImpl(HeadersMapOptConstRef map, envoy_dynamic_module_type_module_buffer key,
                        envoy_dynamic_module_type_envoy_buffer* result, size_t index,
                        size_t* optional_size) {
  if (!map.has_value()) {
    *result = {.ptr = nullptr, .length = 0};
    if (optional_size != nullptr) {
      *optional_size = 0;
    }
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);

  // TODO: we might want to avoid copying the key here by trusting the key is already lower case.
  const auto values = map->get(Envoy::Http::LowerCaseString(key_view));
  if (optional_size != nullptr) {
    *optional_size = values.size();
  }

  if (index >= values.size()) {
    *result = {.ptr = nullptr, .length = 0};
    return false;
  }

  const auto value = values[index]->value().getStringView();
  *result = {.ptr = const_cast<char*>(value.data()), .length = value.size()};
  return true;
}

bool addHeaderValueImpl(HeadersMapOptRef map, envoy_dynamic_module_type_module_buffer key,
                        envoy_dynamic_module_type_module_buffer value) {
  if (!map.has_value()) {
    return false;
  }
  if (value.ptr == nullptr || value.length == 0) {
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  map->addCopy(Envoy::Http::LowerCaseString(key_view), value_view);
  return true;
}

bool setHeaderValueImpl(HeadersMapOptRef map, envoy_dynamic_module_type_module_buffer key,
                        envoy_dynamic_module_type_module_buffer value) {
  if (!map.has_value()) {
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);
  if (value.ptr == nullptr || value.length == 0) {
    map->remove(Envoy::Http::LowerCaseString(key_view));
    return true;
  }
  absl::string_view value_view(value.ptr, value.length);
  // TODO: we might want to avoid copying the key here by trusting the key is already lower case.
  map->setCopy(Envoy::Http::LowerCaseString(key_view), value_view);
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

bool headerAsAttribute(HeadersMapOptConstRef map, const Envoy::Http::LowerCaseString& header,
                       envoy_dynamic_module_type_envoy_buffer* result) {
  if (!map.has_value()) {
    return false;
  }
  auto lower_header = header.get();
  return getHeaderValueImpl(map, {.ptr = lower_header.data(), .length = lower_header.size()},
                            result, 0, nullptr);
}

const Buffer::Instance* getBufferByType(DynamicModuleHttpFilter* filter,
                                        envoy_dynamic_module_type_http_body_type body_type) {
  switch (body_type) {
  case envoy_dynamic_module_type_http_body_type_ReceivedRequestBody:
    return filter->current_request_body_;
  case envoy_dynamic_module_type_http_body_type_BufferedRequestBody:
    return filter->decoder_callbacks_->decodingBuffer();
  case envoy_dynamic_module_type_http_body_type_ReceivedResponseBody:
    return filter->current_response_body_;
  case envoy_dynamic_module_type_http_body_type_BufferedResponseBody:
    return filter->encoder_callbacks_->encodingBuffer();
  default:
    return nullptr;
  }
}

bool getSslInfo(
    OptRef<const Network::Connection> connection,
    std::function<OptRef<const std::string>(const Ssl::ConnectionInfoConstSharedPtr)> get_san_func,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (!connection.has_value() || !connection->ssl()) {
    return false;
  }
  const Ssl::ConnectionInfoConstSharedPtr ssl = connection->ssl();
  OptRef<const std::string> ssl_attribute = get_san_func(ssl);
  if (!ssl_attribute.has_value()) {
    return false;
  }
  const std::string& attribute = ssl_attribute.value();
  *result = {attribute.data(), attribute.size()};
  return true;
}

/**
 * Helper to get the metadata namespace from the metadata.
 * @param metadata is the metadata to search in.
 * @param ns is the namespace of the metadata.
 * @return the metadata namespace if it exists, nullptr otherwise.
 *
 * This will be reused by all envoy_dynamic_module_type_metadata_source where
 * each variant differs in the returned type of the metadata. For example, route metadata will
 * return OptRef vs upstream host metadata will return a shared pointer.
 */
const Protobuf::Struct* getMetadataNamespaceImpl(const envoy::config::core::v3::Metadata& metadata,
                                                 envoy_dynamic_module_type_module_buffer ns) {
  absl::string_view namespace_view{ns.ptr, ns.length};
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
                     envoy_dynamic_module_type_module_buffer ns) {
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
    return getMetadataNamespaceImpl(stream_info.dynamicMetadata(), ns);
  }
  case envoy_dynamic_module_type_metadata_source_Route: {
    auto route = stream_info.route();
    if (route) {
      return getMetadataNamespaceImpl(route->metadata(), ns);
    }
    break;
  }
  case envoy_dynamic_module_type_metadata_source_Cluster: {
    auto clusterInfo = callbacks->clusterInfo();
    if (clusterInfo) {
      return getMetadataNamespaceImpl(clusterInfo->metadata(), ns);
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
          return getMetadataNamespaceImpl(*md, ns);
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
          return getMetadataNamespaceImpl(*md, ns);
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
                            envoy_dynamic_module_type_module_buffer ns) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return nullptr;
  }
  auto metadata = stream_info->dynamicMetadata().mutable_filter_metadata();
  absl::string_view namespace_view{ns.ptr, ns.length};
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
                 envoy_dynamic_module_type_module_buffer ns,
                 envoy_dynamic_module_type_module_buffer key) {
  auto metadata_namespace = getMetadataNamespace(filter_envoy_ptr, metadata_source, ns);
  if (!metadata_namespace) {
    return nullptr;
  }
  absl::string_view key_view(key.ptr, key.length);
  auto key_metadata = metadata_namespace->fields().find(key_view);
  if (key_metadata == metadata_namespace->fields().end()) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("key {} not found in metadata namespace", key_view));
    return nullptr;
  }
  return &key_metadata->second;
}

} // namespace

extern "C" {

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_counter(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Stats::Counter& c =
        Stats::Utility::counterFromStatNames(*filter_config->stats_scope_, {main_stat_name});
    *counter_id_ptr = filter_config->addCounter({c});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

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
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto counter = filter->getFilterConfig().getCounterById(id);
    if (!counter.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    counter->add(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }

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
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::Gauge::ImportMode import_mode =
      Stats::Gauge::ImportMode::Accumulate; // TODO: make this configurable?

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Stats::Gauge& g = Stats::Utility::gaugeFromStatNames(*filter_config->stats_scope_,
                                                         {main_stat_name}, import_mode);
    *gauge_id_ptr = filter_config->addGauge({g});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

  Stats::StatNameVec label_names_vec;
  for (size_t i = 0; i < label_names_length; i++) {
    absl::string_view label_name_view(label_names[i].ptr, label_names[i].length);
    label_names_vec.push_back(filter_config->stat_name_pool_.add(label_name_view));
  }
  *gauge_id_ptr = filter_config->addGaugeVec({main_stat_name, label_names_vec, import_mode});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_increment_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto gauge = filter->getFilterConfig().getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->increase(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
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

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_decrement_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto gauge = filter->getFilterConfig().getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->decrease(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
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
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto gauge = filter->getFilterConfig().getGaugeById(id);
    if (!gauge.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    gauge->set(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
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
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  if (filter_config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  absl::string_view name_view(name.ptr, name.length);
  Stats::StatName main_stat_name = filter_config->stat_name_pool_.add(name_view);
  Stats::Histogram::Unit unit =
      Stats::Histogram::Unit::Unspecified; // TODO: make this configurable?

  // Handle the special case where the labels size is zero.
  if (label_names_length == 0) {
    Stats::Histogram& h = Stats::Utility::histogramFromStatNames(*filter_config->stats_scope_,
                                                                 {main_stat_name}, unit);
    *histogram_id_ptr = filter_config->addHistogram({h});
    return envoy_dynamic_module_type_metrics_result_Success;
  }

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
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  // Handle the special case where the labels size is zero.
  if (label_values_length == 0) {
    auto hist = filter->getFilterConfig().getHistogramById(id);
    if (!hist.has_value()) {
      return envoy_dynamic_module_type_metrics_result_MetricNotFound;
    }
    hist->recordValue(value);
    return envoy_dynamic_module_type_metrics_result_Success;
  }
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

bool envoy_dynamic_module_callback_http_get_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* optional_size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeaderValueImpl(getHeaderMapByType(filter, header_type), key, result, index,
                            optional_size);
}

bool envoy_dynamic_module_callback_http_add_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return addHeaderValueImpl(getHeaderMapByType(filter, header_type), key, value);
}

bool envoy_dynamic_module_callback_http_set_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return setHeaderValueImpl(getHeaderMapByType(filter, header_type), key, value);
}

size_t envoy_dynamic_module_callback_http_get_headers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  HeadersMapOptConstRef headers_map = getHeaderMapByType(filter, header_type);
  if (!headers_map.has_value()) {
    return 0;
  }
  return headers_map->size();
}

bool envoy_dynamic_module_callback_http_get_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return getHeadersImpl(getHeaderMapByType(filter, header_type), result_headers);
}

void envoy_dynamic_module_callback_http_send_response(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t status_code,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    envoy_dynamic_module_type_module_buffer body, envoy_dynamic_module_type_module_buffer details) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->isDestroyed()) {
    return;
  }

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
  absl::string_view body_view{body.ptr, body.length};
  absl::string_view details_view{details.ptr, details.length};
  if (details_view.empty()) {
    details_view = "dynamic_module";
  }

  filter->sendLocalReply(static_cast<Http::Code>(status_code), body_view, modify_headers, 0,
                         details_view);
}

void envoy_dynamic_module_callback_http_send_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    bool end_stream) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->isDestroyed()) {
    return;
  }

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
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->isDestroyed()) {
    return;
  }

  Buffer::OwnedImpl buffer(absl::string_view{data.ptr, data.length});
  filter->decoder_callbacks_->encodeData(buffer, end_stream);
}

void envoy_dynamic_module_callback_http_send_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* trailers_vector, size_t trailers_vector_size) {
  DynamicModuleHttpFilter* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->isDestroyed()) {
    return;
  }

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

size_t envoy_dynamic_module_callback_http_get_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = getBufferByType(filter, body_type);
  if (!buffer) {
    return 0;
  }
  return buffer->length();
}

bool envoy_dynamic_module_callback_http_get_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = getBufferByType(filter, body_type);
  if (!buffer) {
    return false;
  }
  bodyBufferToModule(*buffer, result_buffer_vector);
  return true;
}

size_t envoy_dynamic_module_callback_http_get_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto buffer = getBufferByType(filter, body_type);
  if (!buffer) {
    return 0;
  }
  return buffer->getRawSlices(std::nullopt).size();
}

bool envoy_dynamic_module_callback_http_append_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type,
    envoy_dynamic_module_type_module_buffer data) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  absl::string_view data_view{data.ptr, data.length};

  switch (body_type) {
  case envoy_dynamic_module_type_http_body_type_ReceivedRequestBody: {
    if (auto buffer = filter->current_request_body_; buffer != nullptr) {
      buffer->add(data_view);
      return true;
    }
    return false;
  }
  case envoy_dynamic_module_type_http_body_type_BufferedRequestBody: {
    if (auto buffer = filter->decoder_callbacks_->decodingBuffer(); buffer != nullptr) {
      filter->decoder_callbacks_->modifyDecodingBuffer(
          [data_view](Buffer::Instance& buffer) { buffer.add(data_view); });
    } else {
      Buffer::OwnedImpl owned_buffer;
      owned_buffer.add(data_view);
      filter->decoder_callbacks_->addDecodedData(owned_buffer, true);
    }
    return true;
  }
  case envoy_dynamic_module_type_http_body_type_ReceivedResponseBody: {
    if (auto buffer = filter->current_response_body_; buffer != nullptr) {
      buffer->add(data_view);
      return true;
    }
    return false;
  }
  case envoy_dynamic_module_type_http_body_type_BufferedResponseBody: {
    if (auto buffer = filter->encoder_callbacks_->encodingBuffer(); buffer != nullptr) {
      filter->encoder_callbacks_->modifyEncodingBuffer(
          [data_view](Buffer::Instance& buffer) { buffer.add(data_view); });
    } else {
      Buffer::OwnedImpl owned_buffer;
      owned_buffer.add(data_view);
      filter->encoder_callbacks_->addEncodedData(owned_buffer, true);
    }
    return true;
  }
  }
  return false;
}

bool envoy_dynamic_module_callback_http_drain_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type, size_t number_of_bytes) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  switch (body_type) {
  case envoy_dynamic_module_type_http_body_type_ReceivedRequestBody: {
    if (auto buffer = filter->current_request_body_; buffer != nullptr) {
      const auto size = std::min<uint64_t>(buffer->length(), number_of_bytes);
      buffer->drain(size);
      return true;
    }
    return false;
  }
  case envoy_dynamic_module_type_http_body_type_BufferedRequestBody: {
    if (auto buffer = filter->decoder_callbacks_->decodingBuffer(); buffer != nullptr) {
      filter->decoder_callbacks_->modifyDecodingBuffer([number_of_bytes](Buffer::Instance& buffer) {
        auto size = std::min<uint64_t>(buffer.length(), number_of_bytes);
        buffer.drain(size);
      });
      return true;
    }
    return false;
  }
  case envoy_dynamic_module_type_http_body_type_ReceivedResponseBody: {
    if (auto buffer = filter->current_response_body_; buffer != nullptr) {
      const auto size = std::min<uint64_t>(buffer->length(), number_of_bytes);
      buffer->drain(size);
      return true;
    }
    return false;
  }
  case envoy_dynamic_module_type_http_body_type_BufferedResponseBody: {
    if (auto buffer = filter->encoder_callbacks_->encodingBuffer(); buffer != nullptr) {
      filter->encoder_callbacks_->modifyEncodingBuffer([number_of_bytes](Buffer::Instance& buffer) {
        auto size = std::min<uint64_t>(buffer.length(), number_of_bytes);
        buffer.drain(size);
      });
      return true;
    }
    return false;
  }
  }
  return false;
}

void envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double value) {
  auto metadata_namespace = getDynamicMetadataNamespace(filter_envoy_ptr, ns);
  if (!metadata_namespace) {
    // If stream info is not available, we cannot guarantee that the namespace is created.
    // TODO(wbpcode): this should never happen and we should simplify this.
    return;
  }
  absl::string_view key_view{key.ptr, key.length};
  Protobuf::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_number_value(value);
  metadata_namespace->MergeFrom(metadata_value);
}

bool envoy_dynamic_module_callback_http_get_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double* result) {
  const auto key_metadata = getMetadataValue(filter_envoy_ptr, metadata_source, ns, key);
  if (!key_metadata) {
    return false;
  }
  if (!key_metadata->has_number_value()) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
        fmt::format("key {} is not a number", absl::string_view(key.ptr, key.length)));
    return false;
  }
  *result = key_metadata->number_value();
  return true;
}

void envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value) {
  auto metadata_namespace = getDynamicMetadataNamespace(filter_envoy_ptr, ns);
  if (!metadata_namespace) {
    // If stream info is not available, we cannot guarantee that the namespace is created.
    // TODO(wbpcode): this should never happen and we should simplify this.
    return;
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  Protobuf::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_string_value(value_view);
  metadata_namespace->MergeFrom(metadata_value);
}

bool envoy_dynamic_module_callback_http_get_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const auto key_metadata = getMetadataValue(filter_envoy_ptr, metadata_source, ns, key);
  if (!key_metadata) {
    return false;
  }
  if (!key_metadata->has_string_value()) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
        fmt::format("key {} is not a string", absl::string_view(key.ptr, key.length)));
    return false;
  }
  const std::string& value = key_metadata->string_value();
  *result = {value.data(), value.size()};
  return true;
}

bool envoy_dynamic_module_callback_http_set_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  stream_info->filterState()->setData(key_view,
                                      std::make_unique<Router::StringAccessorImpl>(value_view),
                                      StreamInfo::FilterState::StateType::ReadOnly);
  return true;
}

bool envoy_dynamic_module_callback_http_get_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto stream_info = filter->streamInfo();
  if (!stream_info) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "stream info is not available");
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);
  auto filter_state = stream_info->filterState()->getDataReadOnly<Router::StringAccessor>(key_view);
  if (!filter_state) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        fmt::format("key not found in filter state", key_view));
    return false;
  }
  absl::string_view str = filter_state->asString();
  *result = {str.data(), str.size()};
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
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  bool ok = false;
  switch (attribute_id) {
  case envoy_dynamic_module_type_attribute_id_RequestProtocol: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto protocol = stream_info->protocol();
      if (protocol.has_value()) {
        const auto& protocol_string_ref = Http::Utility::getProtocolString(protocol.value());
        *result = {protocol_string_ref.data(), protocol_string_ref.size()};
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
        *result = {addr.data(), addr.size()};
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
      *result = {addressProvider.data(), addressProvider.size()};
      ok = true;
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_DestinationAddress: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto addressProvider =
          stream_info->downstreamAddressProvider().localAddress()->asStringView();
      *result = {addressProvider.data(), addressProvider.size()};
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
          *result = {request_id->data(), request_id->size()};
          ok = true;
        }
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestPath: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Path, result);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestHost: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Host, result);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestMethod: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Method, result);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestScheme: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().Scheme, result);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestReferer: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::CustomHeaders::get().Referer,
                           result);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestUserAgent: {
    ok = headerAsAttribute(filter->requestHeaders(), Envoy::Http::Headers::get().UserAgent, result);
    break;
  }
  case envoy_dynamic_module_type_attribute_id_RequestUrlPath: {
    RequestHeaderMapOptRef headers = filter->requestHeaders();
    if (headers.has_value()) {
      const absl::string_view path = headers->getPathValue();
      size_t query_offset = path.find('?');
      if (query_offset == absl::string_view::npos) {
        *result = {path.data(), path.length()};
      } else {
        const auto url_path = path.substr(0, query_offset);
        *result = {url_path.data(), url_path.length()};
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
        *result = {query.data(), query.length()};
        ok = true;
      }
    }
    break;
  }
  case envoy_dynamic_module_type_attribute_id_XdsRouteName: {
    const auto stream_info = filter->streamInfo();
    if (stream_info) {
      const auto& route_name = stream_info->getRouteName();
      *result = {route_name.data(), route_name.size()};
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
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectLocalCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->subjectPeerCertificate();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          return ssl->sha256PeerCertificateDigest();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanLocalCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionDnsSanPeerCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->dnsSansPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->dnsSansPeerCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanLocalCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanLocalCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanLocalCertificate().front();
        },
        result);
  case envoy_dynamic_module_type_attribute_id_ConnectionUriSanPeerCertificate:
    return getSslInfo(
        filter->connection(),
        [](const Ssl::ConnectionInfoConstSharedPtr ssl) -> OptRef<const std::string> {
          if (ssl->uriSanPeerCertificate().empty()) {
            return absl::nullopt;
          }
          return ssl->uriSanPeerCertificate().front();
        },
        result);
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

void envoy_dynamic_module_callback_http_add_custom_flag(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer flag) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  absl::string_view flag_name_view(flag.ptr, flag.length);
  filter->decoder_callbacks_->streamInfo().addCustomFlag(flag_name_view);
}

namespace {

envoy::config::core::v3::SocketOption::SocketState
mapHttpSocketState(envoy_dynamic_module_type_socket_option_state state) {
  switch (state) {
  case envoy_dynamic_module_type_socket_option_state_Prebind:
    return envoy::config::core::v3::SocketOption::STATE_PREBIND;
  case envoy_dynamic_module_type_socket_option_state_Bound:
    return envoy::config::core::v3::SocketOption::STATE_BOUND;
  case envoy_dynamic_module_type_socket_option_state_Listening:
    return envoy::config::core::v3::SocketOption::STATE_LISTENING;
  }
  return envoy::config::core::v3::SocketOption::STATE_PREBIND;
}

bool validateHttpSocketState(envoy_dynamic_module_type_socket_option_state state) {
  return state == envoy_dynamic_module_type_socket_option_state_Prebind ||
         state == envoy_dynamic_module_type_socket_option_state_Bound ||
         state == envoy_dynamic_module_type_socket_option_state_Listening;
}

} // namespace

bool envoy_dynamic_module_callback_http_set_socket_option_int(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, int64_t value) {
  ASSERT(validateHttpSocketState(state));
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->decoder_callbacks_ == nullptr) {
    return false;
  }

  if (direction == envoy_dynamic_module_type_socket_direction_Downstream) {
    // For downstream, apply directly to the existing connection socket
    auto connection = filter->decoder_callbacks_->connection();
    if (!connection.has_value()) {
      return false;
    }
    int int_value = static_cast<int>(value);
    auto value_span = absl::MakeSpan(reinterpret_cast<uint8_t*>(&int_value), sizeof(int_value));
    Network::SocketOptionName option_name(static_cast<int>(level), static_cast<int>(name), "");
    // const_cast is safe here because setSocketOption modifies the underlying socket,
    // not the Connection object's logical state.
    if (!const_cast<Network::Connection&>(*connection).setSocketOption(option_name, value_span)) {
      return false;
    }
  } else {
    // For upstream, add to upstream socket options (applied when connection is established)
    auto option = std::make_shared<Network::SocketOptionImpl>(
        mapHttpSocketState(state),
        Network::SocketOptionName(static_cast<int>(level), static_cast<int>(name), ""),
        static_cast<int>(value));
    Network::Socket::OptionsSharedPtr option_list = std::make_shared<Network::Socket::Options>();
    option_list->push_back(option);
    filter->decoder_callbacks_->addUpstreamSocketOptions(option_list);
  }

  filter->storeSocketOptionInt(level, name, state, direction, value);
  return true;
}

bool envoy_dynamic_module_callback_http_set_socket_option_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction,
    envoy_dynamic_module_type_module_buffer value) {
  ASSERT(validateHttpSocketState(state));
  if (value.ptr == nullptr) {
    return false;
  }
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->decoder_callbacks_ == nullptr) {
    return false;
  }

  absl::string_view value_view(value.ptr, value.length);

  if (direction == envoy_dynamic_module_type_socket_direction_Downstream) {
    // For downstream, apply directly to the existing connection socket
    auto connection = filter->decoder_callbacks_->connection();
    if (!connection.has_value()) {
      return false;
    }
    // Need to copy to a mutable buffer since setSocketOption takes non-const span
    std::vector<uint8_t> mutable_value(value.ptr, value.ptr + value.length);
    auto value_span = absl::MakeSpan(mutable_value);
    Network::SocketOptionName option_name(static_cast<int>(level), static_cast<int>(name), "");
    // const_cast is safe here because setSocketOption modifies the underlying socket,
    // not the Connection object's logical state.
    if (!const_cast<Network::Connection&>(*connection).setSocketOption(option_name, value_span)) {
      return false;
    }
  } else {
    // For upstream, add to upstream socket options (applied when connection is established)
    auto option = std::make_shared<Network::SocketOptionImpl>(
        mapHttpSocketState(state),
        Network::SocketOptionName(static_cast<int>(level), static_cast<int>(name), ""), value_view);
    Network::Socket::OptionsSharedPtr option_list = std::make_shared<Network::Socket::Options>();
    option_list->push_back(option);
    filter->decoder_callbacks_->addUpstreamSocketOptions(option_list);
  }

  filter->storeSocketOptionBytes(level, name, state, direction, value_view);
  return true;
}

bool envoy_dynamic_module_callback_http_get_socket_option_int(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, int64_t* value_out) {
  ASSERT(validateHttpSocketState(state));
  if (value_out == nullptr) {
    return false;
  }
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return filter->tryGetSocketOptionInt(level, name, state, direction, *value_out);
}

bool envoy_dynamic_module_callback_http_get_socket_option_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  ASSERT(validateHttpSocketState(state));
  if (value_out == nullptr) {
    return false;
  }
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  absl::string_view value_view;
  if (!filter->tryGetSocketOptionBytes(level, name, state, direction, value_view)) {
    return false;
  }
  value_out->ptr = value_view.data();
  value_out->length = value_view.size();
  return true;
}

uint64_t envoy_dynamic_module_callback_http_get_buffer_limit(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return 0;
  }
  return callbacks->bufferLimit();
}

void envoy_dynamic_module_callback_http_set_buffer_limit(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t limit) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return;
  }
  callbacks->setBufferLimit(limit);
}

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_http_callout(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t* callout_id,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  // Try to get the cluster from the cluster manager for the given cluster name.
  absl::string_view cluster_name_view(cluster_name.ptr, cluster_name.length);

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
  if (body.length > 0) {
    message->body().add(absl::string_view(static_cast<const char*>(body.ptr), body.length));
    message->headers().setContentLength(body.length);
  }
  return filter->sendHttpCallout(callout_id, cluster_name_view, std::move(message),
                                 timeout_milliseconds);
}

envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_start_http_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t* stream_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, bool end_stream, uint64_t timeout_milliseconds) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  // Try to get the cluster from the cluster manager for the given cluster name.
  absl::string_view cluster_name_view(cluster_name.ptr, cluster_name.length);

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
  if (body.length > 0) {
    message->body().add(absl::string_view(body.ptr, body.length));
  }
  return filter->startHttpStream(stream_id_out, cluster_name_view, std::move(message), end_stream,
                                 timeout_milliseconds);
}

void envoy_dynamic_module_callback_http_filter_reset_http_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t stream_id) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  filter->resetHttpStream(stream_id);
}

bool envoy_dynamic_module_callback_http_stream_send_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  // Create a buffer and send the data.
  Buffer::OwnedImpl buffer;
  if (data.length > 0) {
    buffer.add(absl::string_view(data.ptr, data.length));
  }
  return filter->sendStreamData(stream_id, buffer, end_stream);
}

bool envoy_dynamic_module_callback_http_stream_send_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_module_http_header* trailers, size_t trailers_size) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);

  // Construct the trailers.
  std::unique_ptr<RequestTrailerMapImpl> trailer_map = Http::RequestTrailerMapImpl::create();
  for (size_t i = 0; i < trailers_size; i++) {
    const auto& trailer = &trailers[i];
    const absl::string_view key(static_cast<const char*>(trailer->key_ptr), trailer->key_length);
    const absl::string_view value(static_cast<const char*>(trailer->value_ptr),
                                  trailer->value_length);
    trailer_map->addCopy(Http::LowerCaseString(key), value);
  }

  return filter->sendStreamTrailers(stream_id, std::move(trailer_map));
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

envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_http_filter_config_scheduler_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr) {
  auto filter_config = static_cast<DynamicModuleHttpFilterConfig*>(filter_config_envoy_ptr);
  return new DynamicModuleHttpFilterConfigScheduler(filter_config->weak_from_this(),
                                                    filter_config->main_thread_dispatcher_);
}

void envoy_dynamic_module_callback_http_filter_config_scheduler_delete(
    envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleHttpFilterConfigScheduler*>(scheduler_module_ptr);
}

void envoy_dynamic_module_callback_http_filter_config_scheduler_commit(
    envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  DynamicModuleHttpFilterConfigScheduler* scheduler =
      static_cast<DynamicModuleHttpFilterConfigScheduler*>(scheduler_module_ptr);
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

uint32_t envoy_dynamic_module_callback_http_filter_get_worker_index(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  return filter->workerIndex();
}

// ----------------------------- Tracing callbacks -----------------------------

envoy_dynamic_module_type_span_envoy_ptr envoy_dynamic_module_callback_http_get_active_span(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  Tracing::Span& span = filter->activeSpan();
  // Return nullptr if the span is a NullSpan (tracing is not enabled).
  if (dynamic_cast<Tracing::NullSpan*>(&span) != nullptr) {
    return nullptr;
  }
  return &span;
}

void envoy_dynamic_module_callback_http_span_set_tag(
    envoy_dynamic_module_type_span_envoy_ptr span_ptr, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value) {
  if (span_ptr == nullptr) {
    return;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  span->setTag(key_view, value_view);
}

void envoy_dynamic_module_callback_http_span_set_operation(
    envoy_dynamic_module_type_span_envoy_ptr span_ptr,
    envoy_dynamic_module_type_module_buffer operation) {
  if (span_ptr == nullptr) {
    return;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  absl::string_view operation_view(operation.ptr, operation.length);
  span->setOperation(operation_view);
}

void envoy_dynamic_module_callback_http_span_log(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_span_envoy_ptr span_ptr,
    envoy_dynamic_module_type_module_buffer event) {
  if (filter_envoy_ptr == nullptr || span_ptr == nullptr) {
    return;
  }
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  absl::string_view event_view(event.ptr, event.length);
  auto* cb = filter->callbacks();
  if (cb != nullptr) {
    span->log(cb->dispatcher().timeSource().systemTime(), std::string(event_view));
  }
}

void envoy_dynamic_module_callback_http_span_set_sampled(
    envoy_dynamic_module_type_span_envoy_ptr span_ptr, bool sampled) {
  if (span_ptr == nullptr) {
    return;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  span->setSampled(sampled);
}

// Thread-local storage for temporary strings returned by tracing functions.
// These strings are valid until the next call to a tracing function on the same thread.
static thread_local std::string tls_trace_string_storage;

bool envoy_dynamic_module_callback_http_span_get_baggage(
    envoy_dynamic_module_type_span_envoy_ptr span_ptr, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (span_ptr == nullptr || result == nullptr) {
    return false;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  absl::string_view key_view(key.ptr, key.length);
  tls_trace_string_storage = span->getBaggage(key_view);
  if (tls_trace_string_storage.empty()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  result->ptr = tls_trace_string_storage.data();
  result->length = tls_trace_string_storage.size();
  return true;
}

void envoy_dynamic_module_callback_http_span_set_baggage(
    envoy_dynamic_module_type_span_envoy_ptr span_ptr, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value) {
  if (span_ptr == nullptr) {
    return;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  span->setBaggage(key_view, value_view);
}

bool envoy_dynamic_module_callback_http_span_get_trace_id(
    envoy_dynamic_module_type_span_envoy_ptr span_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (span_ptr == nullptr || result == nullptr) {
    return false;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  tls_trace_string_storage = span->getTraceId();
  if (tls_trace_string_storage.empty()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  result->ptr = tls_trace_string_storage.data();
  result->length = tls_trace_string_storage.size();
  return true;
}

bool envoy_dynamic_module_callback_http_span_get_span_id(
    envoy_dynamic_module_type_span_envoy_ptr span_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (span_ptr == nullptr || result == nullptr) {
    return false;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  tls_trace_string_storage = span->getSpanId();
  if (tls_trace_string_storage.empty()) {
    result->ptr = nullptr;
    result->length = 0;
    return false;
  }
  result->ptr = tls_trace_string_storage.data();
  result->length = tls_trace_string_storage.size();
  return true;
}

envoy_dynamic_module_type_child_span_module_ptr envoy_dynamic_module_callback_http_span_spawn_child(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_span_envoy_ptr span_ptr,
    envoy_dynamic_module_type_module_buffer operation_name) {
  if (filter_envoy_ptr == nullptr || span_ptr == nullptr) {
    return nullptr;
  }
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* parent_span = static_cast<Tracing::Span*>(span_ptr);
  absl::string_view operation_view(operation_name.ptr, operation_name.length);

  auto* cb = filter->callbacks();
  if (cb == nullptr) {
    return nullptr;
  }

  // Create a child span with default egress tracing config.
  Tracing::SpanPtr child =
      parent_span->spawnChild(Tracing::EgressConfig::get(), std::string(operation_view),
                              cb->dispatcher().timeSource().systemTime());
  if (child == nullptr) {
    return nullptr;
  }
  // Release ownership to the module - the module is responsible for calling finish.
  return child.release();
}

void envoy_dynamic_module_callback_http_child_span_finish(
    envoy_dynamic_module_type_child_span_module_ptr span_ptr) {
  if (span_ptr == nullptr) {
    return;
  }
  auto* span = static_cast<Tracing::Span*>(span_ptr);
  span->finishSpan();
  delete span;
}

// ------------------- Cluster/Upstream Information Callbacks -------------------------

bool envoy_dynamic_module_callback_http_get_cluster_name(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (result == nullptr) {
    return false;
  }
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return false;
  }
  auto cluster_info = callbacks->clusterInfo();
  if (!cluster_info) {
    return false;
  }
  const std::string& name = cluster_info->name();
  result->ptr = name.data();
  result->length = name.size();
  return true;
}

bool envoy_dynamic_module_callback_http_get_cluster_host_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t priority,
    size_t* total_count, size_t* healthy_count, size_t* degraded_count) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->callbacks();
  if (callbacks == nullptr) {
    return false;
  }
  auto cluster_info = callbacks->clusterInfo();
  if (!cluster_info) {
    return false;
  }
  // Access the thread local cluster to get host information via the filter config's cluster
  // manager.
  if (!filter->hasConfig()) {
    return false;
  }
  auto* tl_cluster =
      filter->getFilterConfig().cluster_manager_.getThreadLocalCluster(cluster_info->name());
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

bool envoy_dynamic_module_callback_http_set_upstream_override_host(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer host, bool strict) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->decoder_callbacks_ == nullptr) {
    return false;
  }
  if (host.ptr == nullptr || host.length == 0) {
    return false;
  }
  absl::string_view host_view(host.ptr, host.length);
  // Validate that the host is a valid IP address.
  if (!Http::Utility::parseAuthority(host_view).is_ip_address_) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "override host is not a valid IP address: {}", host_view);
    return false;
  }
  filter->decoder_callbacks_->setUpstreamOverrideHost(
      std::make_pair(std::string(host_view), strict));
  return true;
}

// ------------------- Stream Control Callbacks -------------------------

void envoy_dynamic_module_callback_http_filter_reset_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_stream_reset_reason reason,
    envoy_dynamic_module_type_module_buffer details) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->decoder_callbacks_ == nullptr) {
    return;
  }
  Http::StreamResetReason envoy_reason;
  switch (reason) {
  case envoy_dynamic_module_type_http_filter_stream_reset_reason_LocalReset:
    envoy_reason = Http::StreamResetReason::LocalReset;
    break;
  case envoy_dynamic_module_type_http_filter_stream_reset_reason_LocalRefusedStreamReset:
    envoy_reason = Http::StreamResetReason::LocalRefusedStreamReset;
    break;
  default:
    envoy_reason = Http::StreamResetReason::LocalReset;
    break;
  }
  absl::string_view details_view;
  if (details.ptr != nullptr && details.length > 0) {
    details_view = absl::string_view(details.ptr, details.length);
  }
  filter->decoder_callbacks_->resetStream(envoy_reason, details_view);
}

void envoy_dynamic_module_callback_http_filter_send_go_away_and_close(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, bool graceful) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->decoder_callbacks_ == nullptr) {
    return;
  }
  filter->decoder_callbacks_->sendGoAwayAndClose(graceful);
}

bool envoy_dynamic_module_callback_http_filter_recreate_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->decoder_callbacks_ == nullptr) {
    return false;
  }

  const Http::ResponseHeaderMap* response_headers = nullptr;

  // If headers are provided, build a response header map for internal redirects.
  std::unique_ptr<Http::ResponseHeaderMapImpl> custom_headers;
  if (headers != nullptr && headers_size > 0) {
    custom_headers = Http::ResponseHeaderMapImpl::create();
    for (size_t i = 0; i < headers_size; ++i) {
      absl::string_view key(headers[i].key_ptr, headers[i].key_length);
      absl::string_view value(headers[i].value_ptr, headers[i].value_length);
      custom_headers->addCopy(Http::LowerCaseString(key), value);
    }
    response_headers = custom_headers.get();
  }

  return filter->decoder_callbacks_->recreateStream(response_headers);
}

void envoy_dynamic_module_callback_http_clear_route_cluster_cache(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleHttpFilter*>(filter_envoy_ptr);
  if (filter->decoder_callbacks_ == nullptr) {
    return;
  }
  auto downstream_callbacks = filter->decoder_callbacks_->downstreamCallbacks();
  if (!downstream_callbacks.has_value()) {
    return;
  }
  downstream_callbacks->refreshRouteCluster();
}

} // extern "C"
} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
