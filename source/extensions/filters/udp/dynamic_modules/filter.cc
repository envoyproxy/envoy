#include "source/extensions/filters/udp/dynamic_modules/filter.h"

#include "source/extensions/filters/udp/dynamic_modules/abi_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

DynamicModuleUdpListenerFilter::DynamicModuleUdpListenerFilter(
    Network::UdpReadFilterCallbacks& callbacks,
    DynamicModuleUdpListenerFilterConfigSharedPtr config, uint32_t worker_index)
    : UdpListenerReadFilter(callbacks), config_(config), worker_index_(worker_index) {
  in_module_filter_ = config_->on_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

DynamicModuleUdpListenerFilter::~DynamicModuleUdpListenerFilter() {
  if (in_module_filter_ != nullptr) {
    config_->on_filter_destroy_(in_module_filter_);
  }
}

Network::FilterStatus DynamicModuleUdpListenerFilter::onData(Network::UdpRecvData& data) {
  if (in_module_filter_ == nullptr) {
    return Network::FilterStatus::Continue;
  }
  current_data_ = &data;
  auto status = config_->on_filter_on_data_(thisAsVoidPtr(), in_module_filter_);
  current_data_ = nullptr;

  if (status == envoy_dynamic_module_type_on_udp_listener_filter_status_StopIteration) {
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::Continue;
}

Network::FilterStatus DynamicModuleUdpListenerFilter::onReceiveError(Api::IoError::IoErrorCode) {
  return Network::FilterStatus::Continue;
}

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
