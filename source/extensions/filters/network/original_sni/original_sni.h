#pragma once

#include "envoy/network/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSni {

/**
 * Implementation of the original_sni filter that sets the original requested server name from
 * the SNI field in the TLS connection.
 */
class OriginalSniFilter : public Network::ReadFilter {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace OriginalSni
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
