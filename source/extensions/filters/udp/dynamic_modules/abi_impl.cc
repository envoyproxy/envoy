// NOLINT(namespace-envoy)
#include "source/extensions/filters/udp/dynamic_modules/abi_impl.h"

#include "envoy/network/address.h"

#include "source/common/network/utility.h"
#include "source/common/stats/utility.h"
#include "source/extensions/filters/udp/dynamic_modules/filter.h"

using Envoy::Extensions::UdpFilters::DynamicModules::DynamicModuleUdpListenerFilter;
using Envoy::Extensions::UdpFilters::DynamicModules::DynamicModuleUdpListenerFilterConfig;

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

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto* data = filter->currentData();
  if (!data || !data->buffer_) {
    return 0;
  }

  return data->buffer_->getRawSlices().size();
}

bool envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* chunks_out) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
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

size_t envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto* data = filter->currentData();
  if (!data || !data->buffer_) {
    return 0;
  }
  return data->buffer_->length();
}

bool envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto* current_data = filter->currentData();
  if (!current_data) {
    return false;
  }

  current_data->buffer_->drain(current_data->buffer_->length());
  if (data.ptr != nullptr && data.length > 0) {
    current_data->buffer_->add(data.ptr, data.length);
  }
  return true;
}

bool envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
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

bool envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto* current_data = filter->currentData();
  const Envoy::Network::Address::Instance* addr = nullptr;

  if (current_data && current_data->addresses_.local_) {
    addr = current_data->addresses_.local_.get();
  } else if (filter->callbacks()) {
    addr = filter->callbacks()->udpListener().localAddress().get();
  }

  if (!addr || addr->type() != Envoy::Network::Address::Type::Ip) {
    return false;
  }

  const std::string& ip_str = addr->ip()->addressAsString();
  address_out->ptr = const_cast<char*>(ip_str.data());
  address_out->length = ip_str.size();
  *port_out = addr->ip()->port();
  return true;
}

bool envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data,
    envoy_dynamic_module_type_module_buffer peer_address, uint32_t peer_port) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);

  Envoy::Buffer::OwnedImpl buffer;
  if (data.ptr && data.length > 0) {
    buffer.add(data.ptr, data.length);
  }

  Envoy::Network::Address::InstanceConstSharedPtr peer_addr;
  if (peer_address.ptr && peer_address.length > 0) {
    std::string ip_str(peer_address.ptr, peer_address.length);
    peer_addr = Envoy::Network::Utility::parseInternetAddressNoThrow(ip_str, peer_port);
    if (!peer_addr) {
      return false;
    }
  } else {
    if (filter->currentData()) {
      peer_addr = filter->currentData()->addresses_.peer_;
    }
  }

  if (!peer_addr) {
    return false;
  }

  const Envoy::Network::Address::Instance* local_addr = nullptr;
  if (filter->currentData()) {
    local_addr = filter->currentData()->addresses_.local_.get();
  }
  if (!local_addr && filter->callbacks()) {
    local_addr = filter->callbacks()->udpListener().localAddress().get();
  }

  if (local_addr && filter->callbacks()) {
    Envoy::Network::UdpSendData udp_data{local_addr->ip(), *peer_addr, buffer};
    filter->callbacks()->udpListener().send(udp_data);
    return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
// Metrics ABI Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleUdpListenerFilterConfig*>(config_envoy_ptr);
  Envoy::Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Counter& c =
      Envoy::Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
  *counter_id_ptr = config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_increment_counter(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto counter = filter->getFilterConfig().getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleUdpListenerFilterConfig*>(config_envoy_ptr);
  Envoy::Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Gauge& g = Envoy::Stats::Utility::gaugeFromStatNames(
      *config->stats_scope_, {main_stat_name}, Envoy::Stats::Gauge::ImportMode::Accumulate);
  *gauge_id_ptr = config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_set_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto gauge = filter->getFilterConfig().getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleUdpListenerFilterConfig*>(config_envoy_ptr);
  Envoy::Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Envoy::Stats::Histogram& h = Envoy::Stats::Utility::histogramFromStatNames(
      *config->stats_scope_, {main_stat_name}, Envoy::Stats::Histogram::Unit::Unspecified);
  *histogram_id_ptr = config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value) {
  auto* filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  auto histogram = filter->getFilterConfig().getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

// -----------------------------------------------------------------------------
// Misc ABI Callbacks
// -----------------------------------------------------------------------------

uint32_t envoy_dynamic_module_callback_udp_listener_filter_get_worker_index(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto filter = static_cast<DynamicModuleUdpListenerFilter*>(filter_envoy_ptr);
  return filter->workerIndex();
}

} // extern "C"
