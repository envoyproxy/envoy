#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DirectResponse {

/**
 * Implementation of a basic direct response filter.
 */
class DirectResponseFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  DirectResponseFilter(const std::string& response, bool keep_open_after_response)
      : response_(response), keep_open_after_response_(keep_open_after_response) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool) override {
    data.drain(data.length());
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().enableHalfClose(true);
  }

private:
  const std::string response_;
  const bool keep_open_after_response_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace DirectResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
