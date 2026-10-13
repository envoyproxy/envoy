#pragma once

#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/common/set_filter_state/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace SetFilterState {

class SetFilterState : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  SetFilterState(Filters::Common::SetFilterState::ConfigSharedPtr on_accept)
      : on_accept_(std::move(on_accept)) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  size_t maxReadBytes() const override { return 0; }
  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override {
    return Network::FilterStatus::Continue;
  }

private:
  const Filters::Common::SetFilterState::ConfigSharedPtr on_accept_;
};

} // namespace SetFilterState
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
