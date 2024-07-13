#include "source/common/upstream/transport_socket_match_impl.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Upstream {

absl::StatusOr<std::unique_ptr<TransportSocketMatcherImpl>> TransportSocketMatcherImpl::create(
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Network::UpstreamTransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<TransportSocketMatcherImpl>(new TransportSocketMatcherImpl(
      socket_matches, factory_context, default_factory, stats_scope, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

TransportSocketMatcherImpl::TransportSocketMatcherImpl(
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Network::UpstreamTransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope,
    absl::Status& creation_status)
    : stats_scope_(stats_scope),
      default_match_("default", std::move(default_factory), generateStats("default")) {
  for (const auto& socket_match : socket_matches) {
    const auto& socket_config = socket_match.transport_socket();
    auto& config_factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(socket_config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        socket_config, factory_context.messageValidationVisitor(), config_factory);
    auto factory_or_error = config_factory.createTransportSocketFactory(*message, factory_context);
    SET_AND_RETURN_IF_NOT_OK(factory_or_error.status(), creation_status);
    FactoryMatch factory_match(socket_match.name(), std::move(factory_or_error.value()),
                               generateStats(absl::StrCat(socket_match.name(), ".")));
    for (const auto& kv : socket_match.match().fields()) {
      factory_match.label_set.emplace_back(kv.first, kv.second);
    }
    matches_.emplace_back(std::move(factory_match));
  }
}

TransportSocketMatchStats TransportSocketMatcherImpl::generateStats(const std::string& prefix) {
  return {ALL_TRANSPORT_SOCKET_MATCH_STATS(POOL_COUNTER_PREFIX(stats_scope_, prefix))};
}

TransportSocketMatcher::MatchData TransportSocketMatcherImpl::resolve(
    const envoy::config::core::v3::Metadata* endpoint_metadata,
    const envoy::config::core::v3::Metadata* locality_metadata) const {
  // We want to check for a match in the endpoint metadata first, since that will always take
  // precedence for transport socket matching.
  for (const auto& match : matches_) {
    if (Config::Metadata::metadataLabelMatch(
            match.label_set, endpoint_metadata,
            Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH, false)) {
      return {*match.factory, match.stats, match.name};
    }
  }

  // If we didn't match on any endpoint-specific metadata, let's check the locality-level metadata.
  for (const auto& match : matches_) {
    if (Config::Metadata::metadataLabelMatch(
            match.label_set, locality_metadata,
            Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH, false)) {
      return {*match.factory, match.stats, match.name};
    }
  }

  return {*default_match_.factory, default_match_.stats, default_match_.name};
}

} // namespace Upstream
} // namespace Envoy
