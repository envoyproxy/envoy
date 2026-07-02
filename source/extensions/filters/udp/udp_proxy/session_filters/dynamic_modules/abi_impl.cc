// NOLINT(namespace-envoy)
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/abi_impl.h"

#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/utility.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/filter.h"

using Envoy::Extensions::UdpFilters::UdpProxy::SessionFilters::DynamicModules::
    DynamicModuleUdpSessionFilter;
using Envoy::Extensions::UdpFilters::UdpProxy::SessionFilters::DynamicModules::
    DynamicModuleUdpSessionFilterConfig;

namespace {

void fillBufferChunks(const Envoy::Buffer::Instance& buffer,
                      envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  Envoy::Buffer::RawSliceVector raw_slices = buffer.getRawSlices();
  size_t counter = 0;
  for (const auto& slice : raw_slices) {
    result_buffer_vector[counter].ptr = static_cast<char*>(slice.mem_);
    result_buffer_vector[counter].length = slice.len_;
    counter++;
  }
}

// Returns the mutable dynamic metadata struct for the namespace, creating it if absent. Returns
// nullptr if the session stream info is not available.
Envoy::Protobuf::Struct*
getDynamicMetadataNamespace(envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
                            envoy_dynamic_module_type_module_buffer ns) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->sessionCallbacks();
  if (callbacks == nullptr) {
    return nullptr;
  }
  auto* metadata = callbacks->streamInfo().dynamicMetadata().mutable_filter_metadata();
  absl::string_view namespace_view{ns.ptr, ns.length};
  auto metadata_namespace = metadata->find(namespace_view);
  if (metadata_namespace == metadata->end()) {
    metadata_namespace = metadata->emplace(namespace_view, Envoy::Protobuf::Struct{}).first;
  }
  return &metadata_namespace->second;
}

// Builds a UdpRecvData for datagram injection. Returns false if either address fails to parse.
bool buildRecvData(DynamicModuleUdpSessionFilter* filter, envoy_dynamic_module_type_module_buffer data,
                   envoy_dynamic_module_type_module_buffer peer_address, uint32_t peer_port,
                   envoy_dynamic_module_type_module_buffer local_address, uint32_t local_port,
                   Envoy::Network::UdpRecvData& out) {
  if (peer_address.ptr == nullptr || peer_address.length == 0 || local_address.ptr == nullptr ||
      local_address.length == 0) {
    return false;
  }
  out.addresses_.peer_ = Envoy::Network::Utility::parseInternetAddressNoThrow(
      std::string(peer_address.ptr, peer_address.length), peer_port);
  out.addresses_.local_ = Envoy::Network::Utility::parseInternetAddressNoThrow(
      std::string(local_address.ptr, local_address.length), local_port);
  if (!out.addresses_.peer_ || !out.addresses_.local_) {
    return false;
  }
  out.buffer_ = std::make_unique<Envoy::Buffer::OwnedImpl>();
  if (data.ptr != nullptr && data.length > 0) {
    out.buffer_->add(data.ptr, data.length);
  }
  out.receive_time_ = filter->getFilterConfig().timeSource().monotonicTime();
  return true;
}

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_udp_session_filter_get_datagram_data_chunks_size(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* data = filter->currentData();
  if (!data || !data->buffer_) {
    return 0;
  }
  return data->buffer_->getRawSlices().size();
}

bool envoy_dynamic_module_callback_udp_session_filter_get_datagram_data_chunks(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* chunks_out) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* data = filter->currentData();
  if (!data || !data->buffer_) {
    return false;
  }
  if (chunks_out == nullptr) {
    return false;
  }
  fillBufferChunks(*data->buffer_, chunks_out);
  return true;
}

size_t envoy_dynamic_module_callback_udp_session_filter_get_datagram_data_size(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* data = filter->currentData();
  if (!data || !data->buffer_) {
    return 0;
  }
  return data->buffer_->length();
}

bool envoy_dynamic_module_callback_udp_session_filter_set_datagram_data(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* current_data = filter->currentData();
  if (!current_data || !current_data->buffer_) {
    return false;
  }
  current_data->buffer_->drain(current_data->buffer_->length());
  if (data.ptr != nullptr && data.length > 0) {
    current_data->buffer_->add(data.ptr, data.length);
  }
  return true;
}

bool envoy_dynamic_module_callback_udp_session_filter_get_peer_address(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* current_data = filter->currentData();
  if (!current_data || !current_data->addresses_.peer_) {
    return false;
  }
  const auto& addr = *current_data->addresses_.peer_;
  if (addr.type() != Envoy::Network::Address::Type::Ip) {
    return false;
  }
  const std::string& ip_str = addr.ip()->addressAsString();
  address_out->ptr = const_cast<char*>(ip_str.data());
  address_out->length = ip_str.size();
  *port_out = addr.ip()->port();
  return true;
}

bool envoy_dynamic_module_callback_udp_session_filter_get_local_address(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* current_data = filter->currentData();
  if (!current_data || !current_data->addresses_.local_) {
    return false;
  }
  const auto& addr = *current_data->addresses_.local_;
  if (addr.type() != Envoy::Network::Address::Type::Ip) {
    return false;
  }
  const std::string& ip_str = addr.ip()->addressAsString();
  address_out->ptr = const_cast<char*>(ip_str.data());
  address_out->length = ip_str.size();
  *port_out = addr.ip()->port();
  return true;
}

// -----------------------------------------------------------------------------
// Session ABI Callbacks
// -----------------------------------------------------------------------------

uint64_t envoy_dynamic_module_callback_udp_session_filter_get_session_id(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->sessionCallbacks();
  if (callbacks == nullptr) {
    return 0;
  }
  return callbacks->sessionId();
}

bool envoy_dynamic_module_callback_udp_session_filter_inject_read_datagram(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data,
    envoy_dynamic_module_type_module_buffer peer_address, uint32_t peer_port,
    envoy_dynamic_module_type_module_buffer local_address, uint32_t local_port) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->readCallbacks();
  if (callbacks == nullptr) {
    return false;
  }
  Envoy::Network::UdpRecvData recv_data;
  if (!buildRecvData(filter, data, peer_address, peer_port, local_address, local_port, recv_data)) {
    return false;
  }
  callbacks->injectDatagramToFilterChain(recv_data);
  return true;
}

bool envoy_dynamic_module_callback_udp_session_filter_inject_write_datagram(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data,
    envoy_dynamic_module_type_module_buffer peer_address, uint32_t peer_port,
    envoy_dynamic_module_type_module_buffer local_address, uint32_t local_port) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->writeCallbacks();
  if (callbacks == nullptr) {
    return false;
  }
  Envoy::Network::UdpRecvData recv_data;
  if (!buildRecvData(filter, data, peer_address, peer_port, local_address, local_port, recv_data)) {
    return false;
  }
  callbacks->injectDatagramToFilterChain(recv_data);
  return true;
}

bool envoy_dynamic_module_callback_udp_session_filter_continue_filter_chain(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->readCallbacks();
  if (callbacks == nullptr) {
    return false;
  }
  return callbacks->continueFilterChain();
}

// -----------------------------------------------------------------------------
// Dynamic Metadata ABI Callbacks
// -----------------------------------------------------------------------------

void envoy_dynamic_module_callback_udp_session_filter_set_dynamic_metadata_string(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* metadata_namespace = getDynamicMetadataNamespace(filter_envoy_ptr, filter_namespace);
  if (metadata_namespace == nullptr) {
    return;
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  Envoy::Protobuf::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_string_value(value_view);
  metadata_namespace->MergeFrom(metadata_value);
}

bool envoy_dynamic_module_callback_udp_session_filter_get_dynamic_metadata_string(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->sessionCallbacks();
  if (callbacks == nullptr) {
    return false;
  }
  std::string namespace_str(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);
  const auto& metadata_map = callbacks->streamInfo().dynamicMetadata().filter_metadata();
  auto namespace_it = metadata_map.find(namespace_str);
  if (namespace_it == metadata_map.end()) {
    return false;
  }
  const auto& fields = namespace_it->second.fields();
  auto field_it = fields.find(key_str);
  if (field_it == fields.end()) {
    return false;
  }
  if (!field_it->second.has_string_value()) {
    return false;
  }
  const auto& value = field_it->second.string_value();
  value_out->ptr = const_cast<char*>(value.data());
  value_out->length = value.size();
  return true;
}

void envoy_dynamic_module_callback_udp_session_filter_set_dynamic_metadata_number(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double value) {
  auto* metadata_namespace = getDynamicMetadataNamespace(filter_envoy_ptr, filter_namespace);
  if (metadata_namespace == nullptr) {
    return;
  }
  absl::string_view key_view(key.ptr, key.length);
  Envoy::Protobuf::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_number_value(value);
  metadata_namespace->MergeFrom(metadata_value);
}

bool envoy_dynamic_module_callback_udp_session_filter_get_dynamic_metadata_number(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double* result) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->sessionCallbacks();
  if (callbacks == nullptr) {
    return false;
  }
  std::string namespace_str(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);
  const auto& metadata_map = callbacks->streamInfo().dynamicMetadata().filter_metadata();
  auto namespace_it = metadata_map.find(namespace_str);
  if (namespace_it == metadata_map.end()) {
    return false;
  }
  const auto& fields = namespace_it->second.fields();
  auto field_it = fields.find(key_str);
  if (field_it == fields.end()) {
    return false;
  }
  if (!field_it->second.has_number_value()) {
    return false;
  }
  *result = field_it->second.number_value();
  return true;
}

void envoy_dynamic_module_callback_udp_session_filter_set_dynamic_metadata_bool(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, bool value) {
  auto* metadata_namespace = getDynamicMetadataNamespace(filter_envoy_ptr, filter_namespace);
  if (metadata_namespace == nullptr) {
    return;
  }
  absl::string_view key_view(key.ptr, key.length);
  Envoy::Protobuf::Struct metadata_value;
  (*metadata_value.mutable_fields())[key_view].set_bool_value(value);
  metadata_namespace->MergeFrom(metadata_value);
}

bool envoy_dynamic_module_callback_udp_session_filter_get_dynamic_metadata_bool(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, bool* result) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto* callbacks = filter->sessionCallbacks();
  if (callbacks == nullptr) {
    return false;
  }
  std::string namespace_str(filter_namespace.ptr, filter_namespace.length);
  std::string key_str(key.ptr, key.length);
  const auto& metadata_map = callbacks->streamInfo().dynamicMetadata().filter_metadata();
  auto namespace_it = metadata_map.find(namespace_str);
  if (namespace_it == metadata_map.end()) {
    return false;
  }
  const auto& fields = namespace_it->second.fields();
  auto field_it = fields.find(key_str);
  if (field_it == fields.end()) {
    return false;
  }
  if (!field_it->second.has_bool_value()) {
    return false;
  }
  *result = field_it->second.bool_value();
  return true;
}

// -----------------------------------------------------------------------------
// Metrics ABI Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_define_counter(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  Envoy::Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Counter& c =
      Envoy::Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
  *counter_id_ptr = config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_increment_counter(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto counter = filter->getFilterConfig().getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_define_gauge(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  Envoy::Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Gauge& g = Envoy::Stats::Utility::gaugeFromStatNames(
      *config->stats_scope_, {main_stat_name}, Envoy::Stats::Gauge::ImportMode::Accumulate);
  *gauge_id_ptr = config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_set_gauge(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_increment_gauge(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_decrement_gauge(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_define_histogram(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  if (config->stat_creation_frozen_) {
    return envoy_dynamic_module_type_metrics_result_Frozen;
  }
  Envoy::Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Histogram& h = Envoy::Stats::Utility::histogramFromStatNames(
      *config->stats_scope_, {main_stat_name}, Envoy::Stats::Histogram::Unit::Unspecified);
  *histogram_id_ptr = config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_record_histogram_value(
    envoy_dynamic_module_type_udp_session_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpSessionFilter*>(filter_envoy_ptr);
  auto histogram = filter->getFilterConfig().getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_increment_counter(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  auto counter = config->getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_increment_gauge(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_decrement_gauge(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_set_gauge(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_session_filter_config_record_histogram_value(
    envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleUdpSessionFilterConfig*>(config_envoy_ptr);
  auto histogram = config->getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"
