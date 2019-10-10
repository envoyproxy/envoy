#pragma once

#include <string>
#include <vector>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

class TransportSocketMatcherImpl : public Logger::Loggable<Logger::Id::upstream>,
                                   public TransportSocketMatcher {
public:
  struct FactoryMatch {
    FactoryMatch(std::string match_name, Network::TransportSocketFactoryPtr socket_factory,
                 TransportSocketMatchStats match_stats)
        : name(std::move(match_name)), factory(std::move(socket_factory)), stats(match_stats) {}
    const std::string name;
    Network::TransportSocketFactoryPtr factory;
    Config::Metadata::LabelSet label_set;
    mutable TransportSocketMatchStats stats;
  };

  TransportSocketMatcherImpl(
      const Protobuf::RepeatedPtrField<envoy::api::v2::Cluster_TransportSocketMatch>&
          socket_matches,
      Server::Configuration::TransportSocketFactoryContext& factory_context,
      Network::TransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope);

  MatchData resolve(const envoy::api::v2::core::Metadata& metadata) const override;

protected:
  TransportSocketMatchStats generateStats(const std::string& prefix);
  Stats::Scope& stats_scope_;
  FactoryMatch default_match_;
  std::vector<FactoryMatch> matches_;
};

} // namespace Upstream
} // namespace Envoy
