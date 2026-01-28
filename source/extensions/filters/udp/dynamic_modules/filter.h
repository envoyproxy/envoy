#pragma once

#include <sys/types.h>

#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/udp/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

class DynamicModuleUdpListenerFilter : public Network::UdpListenerReadFilter,
                                       public Logger::Loggable<Logger::Id::filter> {
public:
  DynamicModuleUdpListenerFilter(Network::UdpReadFilterCallbacks& callbacks,
                                 DynamicModuleUdpListenerFilterConfigSharedPtr config,
                                 uint32_t worker_index);
  ~DynamicModuleUdpListenerFilter() override;

  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData& data) override;
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode error_code) override;

  envoy_dynamic_module_type_udp_listener_filter_envoy_ptr thisAsVoidPtr() {
    return static_cast<void*>(this);
  }

  Network::UdpRecvData* currentData() { return current_data_; }
  Network::UdpReadFilterCallbacks* callbacks() { return read_callbacks_; }

  // Get the filter config for metrics access.
  DynamicModuleUdpListenerFilterConfig& getFilterConfig() const { return *config_; }

#ifdef ENVOY_ENABLE_FULL_PROTOS
  // Test-only method to set current_data_ for ABI callback testing.
  void setCurrentDataForTest(Network::UdpRecvData* data) { current_data_ = data; }
#endif

  uint32_t workerIndex() const { return worker_index_; }

private:
  const DynamicModuleUdpListenerFilterConfigSharedPtr config_;
  envoy_dynamic_module_type_udp_listener_filter_module_ptr in_module_filter_{nullptr};
  Network::UdpRecvData* current_data_{nullptr};
  uint32_t worker_index_;
};

using DynamicModuleUdpListenerFilterSharedPtr = std::shared_ptr<DynamicModuleUdpListenerFilter>;

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
