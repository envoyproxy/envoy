#pragma once

#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

class TransportSocketMatcherImpl : public Logger::Loggable<Logger::Id::upstream>,
                                   public TransportSocketMatcher {
public:
  static absl::StatusOr<std::unique_ptr<TransportSocketMatcherImpl>> create(
      const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
          socket_matches,
      Server::Configuration::TransportSocketFactoryContext& factory_context,
      Network::UpstreamTransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope);

  struct FactoryMatch {
    FactoryMatch(std::string match_name, Network::UpstreamTransportSocketFactoryPtr socket_factory,
                 TransportSocketMatchStats match_stats)
        : name(std::move(match_name)), factory(std::move(socket_factory)), stats(match_stats) {}
    const std::string name;
    Network::UpstreamTransportSocketFactoryPtr factory;
    Config::Metadata::LabelSet label_set;
    mutable TransportSocketMatchStats stats;
  };

  MatchData resolve(const envoy::config::core::v3::Metadata* endpoint_metadata,
                    const envoy::config::core::v3::Metadata* locality_metadata) const override;

  bool allMatchesSupportAlpn() const override {
    if (!default_match_.factory->supportsAlpn()) {
      return false;
    }
    for (const auto& match : matches_) {
      if (!match.factory->supportsAlpn()) {
        return false;
      }
    }
    return true;
  }

protected:
  TransportSocketMatcherImpl(
      const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
          socket_matches,
      Server::Configuration::TransportSocketFactoryContext& factory_context,
      Network::UpstreamTransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope,
      absl::Status& creation_status);

  TransportSocketMatchStats generateStats(const std::string& prefix);
  Stats::Scope& stats_scope_;
  FactoryMatch default_match_;
  std::vector<FactoryMatch> matches_;
};

} // namespace Upstream
} // namespace Envoy
