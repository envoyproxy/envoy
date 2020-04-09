#pragma once

#include "envoy/network/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DirectResponse {

/**
 * Implementation of a basic direct response filter.
 */
class DirectResponseFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  DirectResponseFilter(const std::string& response) : response_(response) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  const std::string response_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace DirectResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
