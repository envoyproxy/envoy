#include "common/upstream/transport_socket_match_impl.h"

#include "envoy/server/transport_socket_config.h"

#include "common/config/utility.h"

namespace Envoy {
namespace Upstream {

TransportSocketMatcherImpl::TransportSocketMatcherImpl(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Cluster_TransportSocketMatch>& socket_matches,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Network::TransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope)
    : stats_scope_(stats_scope),
      default_match_("default", std::move(default_factory), generateStats("default")) {
  for (const auto& socket_match : socket_matches) {
    const auto& socket_config = socket_match.transport_socket();
    auto& config_factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(socket_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        socket_config, factory_context.messageValidationVisitor(), config_factory);
    FactoryMatch factory_match(
        socket_match.name(), config_factory.createTransportSocketFactory(*message, factory_context),
        generateStats(socket_match.name() + "."));
    for (const auto& kv : socket_match.match().fields()) {
      factory_match.label_set.emplace_back(kv.first, kv.second);
    }
    matches_.emplace_back(std::move(factory_match));
  }
}

TransportSocketMatchStats TransportSocketMatcherImpl::generateStats(const std::string& prefix) {
  return {ALL_TRANSPORT_SOCKET_MATCH_STATS(POOL_COUNTER_PREFIX(stats_scope_, prefix))};
}

TransportSocketMatcher::MatchData
TransportSocketMatcherImpl::resolve(const envoy::api::v2::core::Metadata& metadata) const {
  for (const auto& match : matches_) {
    if (Config::Metadata::metadataLabelMatch(
            match.label_set, metadata,
            Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH, false)) {
      return MatchData(*match.factory, match.stats, match.name);
    }
  }
  return MatchData(*default_match_.factory, default_match_.stats, default_match_.name);
}

} // namespace Upstream
} // namespace Envoy
