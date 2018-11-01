#pragma once

#include "envoy/config/filter/network/sni_cluster/v2/sni_cluster.pb.h"
#include "envoy/config/filter/network/sni_cluster/v2/sni_cluster.pb.validate.h"
#include "envoy/network/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

/**
 * Configuration for the SNI Cluster network filter.
 */
class SniClusterFilterConfig {
public:
  SniClusterFilterConfig(
      const envoy::config::filter::network::sni_cluster::v2::SniCluster& proto_config);

  envoy::type::Substitution sniSubstitution() const { return sni_substitution_; }

private:
  envoy::type::Substitution sni_substitution_;
};

typedef std::shared_ptr<SniClusterFilterConfig> SniClusterFilterConfigSharedPtr;

/**
 * Implementation of the sni_cluster filter that sets the upstream cluster name from
 * the SNI field in the TLS connection.
 */
class SniClusterFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  SniClusterFilter(SniClusterFilterConfigSharedPtr config) : config_(config) {}
  ~SniClusterFilter() {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  SniClusterFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
