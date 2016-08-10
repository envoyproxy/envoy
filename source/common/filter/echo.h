#pragma once

#include "envoy/network/filter.h"

#include "common/common/logger.h"

namespace Filter {

/**
 * Implementation of a basic echo filter.
 */
class Echo : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // Filter
