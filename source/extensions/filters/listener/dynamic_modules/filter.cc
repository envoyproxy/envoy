#include "source/extensions/filters/listener/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

namespace {

Network::FilterStatus
toEnvoyFilterStatus(envoy_dynamic_module_type_on_listener_filter_status status) {
  switch (status) {
  case envoy_dynamic_module_type_on_listener_filter_status_Continue:
    return Network::FilterStatus::Continue;
  case envoy_dynamic_module_type_on_listener_filter_status_StopIteration:
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::Continue;
}

} // namespace

DynamicModuleListenerFilter::DynamicModuleListenerFilter(
    DynamicModuleListenerFilterConfigSharedPtr config)
    : config_(config) {}

DynamicModuleListenerFilter::~DynamicModuleListenerFilter() { destroy(); }

void DynamicModuleListenerFilter::initializeInModuleFilter() {
  in_module_filter_ = config_->on_listener_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleListenerFilter::destroy() {
  if (in_module_filter_ != nullptr) {
    config_->on_listener_filter_destroy_(in_module_filter_);
    in_module_filter_ = nullptr;
  }
  destroyed_ = true;
}

Network::FilterStatus DynamicModuleListenerFilter::onAccept(Network::ListenerFilterCallbacks& cb) {
  callbacks_ = &cb;

  if (in_module_filter_ == nullptr) {
    // Module failed to create filter, close the connection.
    cb.socket().ioHandle().close();
    return Network::FilterStatus::StopIteration;
  }

  auto status = config_->on_listener_filter_on_accept_(thisAsVoidPtr(), in_module_filter_);
  return toEnvoyFilterStatus(status);
}

Network::FilterStatus DynamicModuleListenerFilter::onData(Network::ListenerFilterBuffer& buffer) {
  if (in_module_filter_ == nullptr) {
    return Network::FilterStatus::Continue;
  }

  // Set the current buffer for ABI callbacks.
  current_buffer_ = &buffer;
  auto raw_slice = buffer.rawSlice();
  auto status =
      config_->on_listener_filter_on_data_(thisAsVoidPtr(), in_module_filter_, raw_slice.len_);
  current_buffer_ = nullptr;

  return toEnvoyFilterStatus(status);
}

void DynamicModuleListenerFilter::onClose() {
  if (in_module_filter_ == nullptr) {
    return;
  }
  config_->on_listener_filter_on_close_(thisAsVoidPtr(), in_module_filter_);
}

size_t DynamicModuleListenerFilter::maxReadBytes() const {
  if (in_module_filter_ == nullptr) {
    return 0;
  }
  return config_->on_listener_filter_get_max_read_bytes_(
      const_cast<DynamicModuleListenerFilter*>(this)->thisAsVoidPtr(), in_module_filter_);
}

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
