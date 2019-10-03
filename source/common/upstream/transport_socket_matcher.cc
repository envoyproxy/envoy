#include "common/upstream/transport_socket_matcher.h"

#include "envoy/server/transport_socket_config.h"

#include "common/config/utility.h"

namespace Envoy {
namespace Upstream {

TransportSocketMatcher::TransportSocketMatcher(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Cluster_TransportSocketMatch>& socket_matches,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Network::TransportSocketFactory& default_factory, Stats::Scope& stats_scope)
    : default_socket_factory_(default_factory), stats_scope_(stats_scope) {
  for (const auto& socket_match : socket_matches) {
    FactoryMatch factory_match(socket_match.name(), generateStats(socket_match.name() + "."));
    for (const auto& kv : socket_match.match().fields()) {
      factory_match.label_set.emplace_back(kv.first, kv.second);
    }
    const auto& socket_config = socket_match.transport_socket();
    auto& config_factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(socket_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        socket_config, factory_context.messageValidationVisitor(), config_factory);
    factory_match.factory = config_factory.createTransportSocketFactory(*message, factory_context);
    matches_.emplace_back(std::move(factory_match));
  }
}

TransportSocketMatchStats TransportSocketMatcher::generateStats(const std::string& prefix) {
  return {ALL_TRANSPORT_SOCKET_MATCHER_STATS(POOL_COUNTER_PREFIX(stats_scope_, prefix))};
}

Network::TransportSocketFactory&
TransportSocketMatcher::resolve(const std::string& endpoint_addr,
                                const envoy::api::v2::core::Metadata& metadata) {
  for (const auto& socket_factory_match : matches_) {
    if (Config::Metadata::metadataLabelMatch(
            socket_factory_match.label_set, metadata,
            Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH, false)) {
      socket_factory_match.stats.total_match_count_.inc();
      ENVOY_LOG(debug, "transport socket match found: name {}, metadata {}, address {}",
                socket_factory_match.name, metadata.DebugString(), endpoint_addr);
      return *socket_factory_match.factory;
    }
  }
  ENVOY_LOG(debug, "transport socket match, no match, return default: metadata {}, address {}",
            metadata.DebugString(), endpoint_addr);
  return default_socket_factory_;
}

} // namespace Upstream
} // namespace Envoy
