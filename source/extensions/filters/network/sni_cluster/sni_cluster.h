#pragma once

#include "envoy/network/filter.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

/**
 * Implementation of the sni_cluster filter that sets the upstream cluster name from
 * the SNI field in the TLS connection.
 */
class SniClusterFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
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

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
