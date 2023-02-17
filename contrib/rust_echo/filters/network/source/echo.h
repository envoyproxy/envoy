#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"

#include "contrib/rust_echo/filters/network/source/echo.rs.h"
#include "rust/cxx.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Echo {

using EntryPointFunc = std::function<void(Network::ReadFilterCallbacks*, Executor*)>;

class RustExecutorFilter : public Network::ReadFilter {
public:
  explicit RustExecutorFilter(EntryPointFunc entry_point) : entry_point_(entry_point) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& buffer, bool end_stream) override {
    executor_->onData(buffer, end_stream);
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override {
    entry_point_(callbacks_, &*executor_);

    executor_->poll();
    return Network::FilterStatus::Continue;
  }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    executor_ = std::make_unique<Executor>(callbacks.connection().dispatcher());
  }

private:
  EntryPointFunc entry_point_;
  std::unique_ptr<Executor> executor_;
  Network::ReadFilterCallbacks* callbacks_;
};

/**
 * The echo filter itself just delegates execution to the Rust entrypoint.
 */
class Filter : public RustExecutorFilter {
public:
  Filter() : RustExecutorFilter(on_new_connection) {}
};

} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
