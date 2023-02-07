#pragma once

#include "envoy/network/filter.h"
#include "rust/cxx.h"
#include "source/extensions/filters/network/echo/echo.rs.h"

#include "source/common/common/logger.h"

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
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    return on_data(**echo_filter_, data, end_stream);
  }
  Network::FilterStatus onNewConnection() override { return on_new_connection(**echo_filter_); }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    echo_filter_ = absl::make_optional(create_filter(callbacks));
  }

private:
  absl::optional<rust::Box<EchoFilter>> echo_filter_{};
};

} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
