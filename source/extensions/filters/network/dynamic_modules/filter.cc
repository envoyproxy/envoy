#include "source/extensions/filters/network/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

namespace {

Network::FilterStatus
toEnvoyFilterStatus(envoy_dynamic_module_type_on_network_filter_data_status status) {
  switch (status) {
  case envoy_dynamic_module_type_on_network_filter_data_status_Continue:
    return Network::FilterStatus::Continue;
  case envoy_dynamic_module_type_on_network_filter_data_status_StopIteration:
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::Continue;
}

envoy_dynamic_module_type_network_connection_event
toAbiConnectionEvent(Network::ConnectionEvent event) {
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    return envoy_dynamic_module_type_network_connection_event_RemoteClose;
  case Network::ConnectionEvent::LocalClose:
    return envoy_dynamic_module_type_network_connection_event_LocalClose;
  case Network::ConnectionEvent::Connected:
    return envoy_dynamic_module_type_network_connection_event_Connected;
  case Network::ConnectionEvent::ConnectedZeroRtt:
    return envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt;
  }
  return envoy_dynamic_module_type_network_connection_event_LocalClose;
}

} // namespace

DynamicModuleNetworkFilter::DynamicModuleNetworkFilter(
    DynamicModuleNetworkFilterConfigSharedPtr config)
    : config_(config) {}

DynamicModuleNetworkFilter::~DynamicModuleNetworkFilter() { destroy(); }

void DynamicModuleNetworkFilter::initializeInModuleFilter() {
  in_module_filter_ = config_->on_network_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleNetworkFilter::destroy() {
  if (in_module_filter_ != nullptr) {
    config_->on_network_filter_destroy_(in_module_filter_);
    in_module_filter_ = nullptr;
  }
  destroyed_ = true;
}

void DynamicModuleNetworkFilter::initializeReadFilterCallbacks(
    Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  // Register for connection events.
  read_callbacks_->connection().addConnectionCallbacks(*this);
}

void DynamicModuleNetworkFilter::initializeWriteFilterCallbacks(
    Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

Network::FilterStatus DynamicModuleNetworkFilter::onNewConnection() {
  if (in_module_filter_ == nullptr) {
    if (read_callbacks_ != nullptr) {
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    return Network::FilterStatus::StopIteration;
  }
  auto status = config_->on_network_filter_new_connection_(thisAsVoidPtr(), in_module_filter_);
  return toEnvoyFilterStatus(status);
}

Network::FilterStatus DynamicModuleNetworkFilter::onData(Buffer::Instance& data, bool end_stream) {
  if (in_module_filter_ == nullptr) {
    return Network::FilterStatus::Continue;
  }
  // Set the current read buffer for ABI callbacks.
  current_read_buffer_ = &data;
  auto status = config_->on_network_filter_read_(thisAsVoidPtr(), in_module_filter_, data.length(),
                                                 end_stream);
  current_read_buffer_ = nullptr;
  return toEnvoyFilterStatus(status);
}

Network::FilterStatus DynamicModuleNetworkFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  if (in_module_filter_ == nullptr) {
    return Network::FilterStatus::Continue;
  }
  // Set the current write buffer for ABI callbacks.
  current_write_buffer_ = &data;
  auto status = config_->on_network_filter_write_(thisAsVoidPtr(), in_module_filter_, data.length(),
                                                  end_stream);
  current_write_buffer_ = nullptr;
  return toEnvoyFilterStatus(status);
}

void DynamicModuleNetworkFilter::onEvent(Network::ConnectionEvent event) {
  if (in_module_filter_ == nullptr) {
    return;
  }
  config_->on_network_filter_event_(thisAsVoidPtr(), in_module_filter_,
                                    toAbiConnectionEvent(event));
}

void DynamicModuleNetworkFilter::onAboveWriteBufferHighWatermark() {
  // Not currently exposed to dynamic modules.
}

void DynamicModuleNetworkFilter::onBelowWriteBufferLowWatermark() {
  // Not currently exposed to dynamic modules.
}

void DynamicModuleNetworkFilter::continueReading() {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->continueReading();
  }
}

void DynamicModuleNetworkFilter::close(Network::ConnectionCloseType close_type) {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->connection().close(close_type);
  }
}

void DynamicModuleNetworkFilter::write(Buffer::Instance& data, bool end_stream) {
  if (read_callbacks_ != nullptr) {
    read_callbacks_->connection().write(data, end_stream);
  }
}

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
