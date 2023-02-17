#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"

#include "contrib/rust_echo/filters/network/source/echo.rs.h"
#include "rust/cxx.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Echo {

/**
 * Implementation of a basic echo filter.
 */
class Filter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& buffer, bool end_stream) override {
    executor_->onData(buffer, end_stream);
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override {
    on_new_connection(callbacks_, &*executor_);

    executor_->poll();
    return Network::FilterStatus::Continue;
  }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    executor_ = std::make_unique<Executor>(callbacks.connection().dispatcher());
  }

private:
  std::unique_ptr<Executor> executor_;
  Network::ReadFilterCallbacks* callbacks_;
};

} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
