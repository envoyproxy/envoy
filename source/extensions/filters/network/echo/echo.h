#pragma once

#include "envoy/network/filter.h"
#include "rust/cxx.h"
#include "source/extensions/filters/network/rust_executor/echo.rs.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RustExecutor {

/**
 * Implementation of a basic echo filter.
 */
class Filter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override {
    on_new_connection(**echo_filter_, *executor_);
    return Network::FilterStatus::Continue;
  }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    echo_filter_ = absl::make_optional(create_filter(callbacks));
    executor_ = std::make_unique<Executor>(callbacks.connection().dispatcher());
  }

private:
  absl::optional<rust::Box<EchoFilter>> echo_filter_{};
  std::unique_ptr<Executor> executor_;
};

} // namespace RustExecutor
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
