#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/filter.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/abi_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicModules {

namespace {
Network::UdpSessionReadFilterStatus
toReadFilterStatus(envoy_dynamic_module_type_on_udp_session_read_filter_status status) {
  if (status == envoy_dynamic_module_type_on_udp_session_read_filter_status_StopIteration) {
    return Network::UdpSessionReadFilterStatus::StopIteration;
  }
  return Network::UdpSessionReadFilterStatus::Continue;
}

Network::UdpSessionWriteFilterStatus
toWriteFilterStatus(envoy_dynamic_module_type_on_udp_session_write_filter_status status) {
  if (status == envoy_dynamic_module_type_on_udp_session_write_filter_status_StopIteration) {
    return Network::UdpSessionWriteFilterStatus::StopIteration;
  }
  return Network::UdpSessionWriteFilterStatus::Continue;
}
} // namespace

DynamicModuleUdpSessionFilter::DynamicModuleUdpSessionFilter(
    DynamicModuleUdpSessionFilterConfigSharedPtr config)
    : config_(config) {
  in_module_filter_ = config_->on_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

DynamicModuleUdpSessionFilter::~DynamicModuleUdpSessionFilter() {
  if (in_module_filter_ != nullptr) {
    config_->on_filter_destroy_(in_module_filter_);
  }
}

Network::UdpSessionReadFilterStatus DynamicModuleUdpSessionFilter::onNewSession() {
  if (in_module_filter_ == nullptr) {
    return Network::UdpSessionReadFilterStatus::Continue;
  }
  return toReadFilterStatus(
      config_->on_filter_on_new_session_(thisAsVoidPtr(), in_module_filter_));
}

Network::UdpSessionReadFilterStatus
DynamicModuleUdpSessionFilter::onData(Network::UdpRecvData& data) {
  if (in_module_filter_ == nullptr) {
    return Network::UdpSessionReadFilterStatus::Continue;
  }
  current_data_ = &data;
  auto status = config_->on_filter_on_data_(thisAsVoidPtr(), in_module_filter_);
  current_data_ = nullptr;
  return toReadFilterStatus(status);
}

Network::UdpSessionWriteFilterStatus
DynamicModuleUdpSessionFilter::onWrite(Network::UdpRecvData& data) {
  if (in_module_filter_ == nullptr) {
    return Network::UdpSessionWriteFilterStatus::Continue;
  }
  current_data_ = &data;
  auto status = config_->on_filter_on_write_(thisAsVoidPtr(), in_module_filter_);
  current_data_ = nullptr;
  return toWriteFilterStatus(status);
}

void DynamicModuleUdpSessionFilter::onSessionCompleteInternal() {
  if (in_module_filter_ == nullptr || config_->on_filter_on_session_complete_ == nullptr) {
    return;
  }
  config_->on_filter_on_session_complete_(thisAsVoidPtr(), in_module_filter_);
}

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
