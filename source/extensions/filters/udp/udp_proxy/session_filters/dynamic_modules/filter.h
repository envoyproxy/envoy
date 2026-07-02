#pragma once

#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/filter_config.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicModules {

class DynamicModuleUdpSessionFilter : public SessionFilters::PassThroughFilter,
                                      public Logger::Loggable<Logger::Id::filter> {
public:
  DynamicModuleUdpSessionFilter(DynamicModuleUdpSessionFilterConfigSharedPtr config);
  ~DynamicModuleUdpSessionFilter() override;

  // Network::UdpSessionReadFilter
  Network::UdpSessionReadFilterStatus onNewSession() override;
  Network::UdpSessionReadFilterStatus onData(Network::UdpRecvData& data) override;

  // Network::UdpSessionWriteFilter
  Network::UdpSessionWriteFilterStatus onWrite(Network::UdpRecvData& data) override;

  // Network::UdpSessionFilterBase
  void onSessionCompleteInternal() override;

  envoy_dynamic_module_type_udp_session_filter_envoy_ptr thisAsVoidPtr() {
    return static_cast<void*>(this);
  }

  // Accessors used by the ABI callbacks.
  Network::UdpRecvData* currentData() { return current_data_; }
  Network::UdpSessionReadFilterCallbacks* readCallbacks() { return read_callbacks_; }
  Network::UdpSessionWriteFilterCallbacks* writeCallbacks() { return write_callbacks_; }
  // Either the read or write callbacks expose the common UdpSessionFilterCallbacks interface.
  Network::UdpSessionFilterCallbacks* sessionCallbacks() {
    if (read_callbacks_ != nullptr) {
      return read_callbacks_;
    }
    return write_callbacks_;
  }

  // Get the filter config for metrics access.
  DynamicModuleUdpSessionFilterConfig& getFilterConfig() const { return *config_; }

#ifdef ENVOY_ENABLE_FULL_PROTOS
  // Test-only method to set current_data_ for ABI callback testing.
  void setCurrentDataForTest(Network::UdpRecvData* data) { current_data_ = data; }
#endif

private:
  const DynamicModuleUdpSessionFilterConfigSharedPtr config_;
  envoy_dynamic_module_type_udp_session_filter_module_ptr in_module_filter_{nullptr};
  Network::UdpRecvData* current_data_{nullptr};
};

using DynamicModuleUdpSessionFilterSharedPtr = std::shared_ptr<DynamicModuleUdpSessionFilter>;

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
