#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/common/set_filter_state/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SetFilterState {

class SetFilterState : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit SetFilterState(const Filters::Common::SetFilterState::ConfigSharedPtr config)
      : config_(config) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  const Filters::Common::SetFilterState::ConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace SetFilterState
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
