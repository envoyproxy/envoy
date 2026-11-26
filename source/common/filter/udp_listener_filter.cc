#include "source/common/filter/udp_listener_filter.h"

#include <algorithm>

#include "envoy/common/exception.h"

namespace Envoy {
namespace Filter {

UdpListenerFilter::UdpListenerFilter(
    Config::ExtensionConfigProvider<Network::UdpListenerFilterFactoryCb>& provider,
    Network::UdpReadFilterCallbacks& callbacks, Stats::Counter& update_failures)
    : UdpListenerReadFilter(callbacks), provider_(provider), update_failures_(update_failures) {
  update_handle_ = provider_.addUpdateCallback([this]() { update(); });
  update();
}

UdpListenerFilter::~UdpListenerFilter() {
  if (!ready_) {
    read_callbacks_->onFilterConfigReadinessChanged(true);
  }
}

void UdpListenerFilter::update() {
  // The dispatcher serializes updates and datagram processing on this worker.
  filters_.clear();
  if (auto factory = provider_.config(); factory.has_value()) {
    std::string error;
    TRY_NEEDS_AUDIT {
      factory.value()(static_cast<Network::UdpListenerFilterManager&>(*this), *read_callbacks_);
    }
    END_TRY
    CATCH(const EnvoyException& e, {
      error = e.what();
      filters_.clear();
    });
    if (filters_.empty() ||
        std::find(filters_.begin(), filters_.end(), nullptr) != filters_.end()) {
      // Worker construction follows main-thread acceptance. Discard partial results until a
      // subsequent configuration supplies a usable filter.
      filters_.clear();
      update_failures_.inc();
      ENVOY_LOG(error, "UDP listener filter {} worker update failed: {}", provider_.name(),
                error.empty() ? "factory produced an empty or null read filter" : error);
    }
  }

  const bool ready = !filters_.empty();
  if (ready != ready_) {
    ready_ = ready;
    read_callbacks_->onFilterConfigReadinessChanged(ready);
  }
}

void UdpListenerFilter::addReadFilter(Network::UdpListenerReadFilterPtr&& filter) {
  filters_.push_back(std::move(filter));
}

Network::FilterStatus UdpListenerFilter::onData(Network::UdpRecvData& data) {
  for (const auto& filter : filters_) {
    if (filter->onData(data) == Network::FilterStatus::StopIteration) {
      return Network::FilterStatus::StopIteration;
    }
  }
  return Network::FilterStatus::Continue;
}

Network::FilterStatus UdpListenerFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  for (const auto& filter : filters_) {
    if (filter->onReceiveError(error_code) == Network::FilterStatus::StopIteration) {
      return Network::FilterStatus::StopIteration;
    }
  }
  return Network::FilterStatus::Continue;
}

} // namespace Filter
} // namespace Envoy
