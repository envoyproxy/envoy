#include "source/common/upstream/transport_socket_match_impl.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/common/matcher/exact_map_matcher.h"
#include "source/common/matcher/matcher.h"
#include "source/common/matcher/prefix_map_matcher.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "xds/type/matcher/v3/matcher.pb.h"

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

absl::StatusOr<std::unique_ptr<TransportSocketMatcherImpl>> TransportSocketMatcherImpl::create(
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    OptRef<const xds::type::matcher::v3::Matcher> transport_socket_matcher,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Network::UpstreamTransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<TransportSocketMatcherImpl>(
      new TransportSocketMatcherImpl(socket_matches, transport_socket_matcher, factory_context,
                                     default_factory, stats_scope, creation_status));
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
  setupLegacySocketMatches(socket_matches, factory_context, creation_status);
}

TransportSocketMatcherImpl::TransportSocketMatcherImpl(
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    OptRef<const xds::type::matcher::v3::Matcher> transport_socket_matcher,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Network::UpstreamTransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope,
    absl::Status& creation_status)
    : stats_scope_(stats_scope),
      default_match_("default", std::move(default_factory), generateStats("default")) {
  if (transport_socket_matcher.has_value()) {
    setupTransportSocketMatcher(transport_socket_matcher.value(), socket_matches, factory_context,
                                creation_status);
  } else {
    setupLegacySocketMatches(socket_matches, factory_context, creation_status);
  }
}

TransportSocketMatchStats
TransportSocketMatcherImpl::generateStats(const std::string& prefix) const {
  return {ALL_TRANSPORT_SOCKET_MATCH_STATS(POOL_COUNTER_PREFIX(stats_scope_, prefix))};
}

TransportSocketMatcher::MatchData TransportSocketMatcherImpl::resolve(
    const envoy::config::core::v3::Metadata* endpoint_metadata,
    const envoy::config::core::v3::Metadata* locality_metadata,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  // If matcher is available, use matcher-based resolution.
  if (matcher_) {
    return resolveUsingMatcher(endpoint_metadata, locality_metadata, transport_socket_options);
  }

  // Fall back to legacy metadata-based matching.
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

void TransportSocketMatcherImpl::setupLegacySocketMatches(
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status) {
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

TransportSocketMatcher::MatchData TransportSocketMatcherImpl::resolveUsingMatcher(
    const envoy::config::core::v3::Metadata* endpoint_metadata,
    const envoy::config::core::v3::Metadata* locality_metadata,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  // Extract filter state from transport socket options if available.
  StreamInfo::FilterStateSharedPtr filter_state;
  if (transport_socket_options) {
    const auto& shared_objects = transport_socket_options->downstreamSharedFilterStateObjects();
    if (!shared_objects.empty()) {
      // Create a temporary filter state to hold the shared objects for matching.
      filter_state = std::make_shared<StreamInfo::FilterStateImpl>(
          StreamInfo::FilterState::LifeSpan::Connection);
      for (const auto& object : shared_objects) {
        filter_state->setData(object.name_, object.data_, object.state_type_,
                              StreamInfo::FilterState::LifeSpan::Connection,
                              object.stream_sharing_);
      }
    }
  }

  Upstream::TransportSocketMatchingData data(endpoint_metadata, locality_metadata,
                                             filter_state.get());
  auto on_match = Matcher::evaluateMatch(*matcher_, data);
  if (on_match.isMatch()) {
    const auto action = on_match.action();
    if (action) {
      const auto& name_action = action->getTyped<TransportSocketNameAction>();
      const std::string& transport_socket_name = name_action.name();
      const auto it = transport_sockets_by_name_.find(transport_socket_name);
      if (it != transport_sockets_by_name_.end()) {
        // Stats should already be pre-generated during configuration.
        auto it_stats = matcher_stats_by_name_.find(transport_socket_name);
        ASSERT(it_stats != matcher_stats_by_name_.end(),
               "Stats should be pre-generated for all transport sockets");
        return {*it->second, it_stats->second, transport_socket_name};
      }
      ENVOY_LOG(warn, "Transport socket '{}' not found, using default", transport_socket_name);
    }
  }

  // Fall back to default if no match or action found.
  return {*default_match_.factory, default_match_.stats, default_match_.name};
}

void TransportSocketMatcherImpl::setupTransportSocketMatcher(
    const xds::type::matcher::v3::Matcher& transport_socket_matcher,
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status) {
  // Build the transport socket factories by name only if using matcher-based selection.
  for (const auto& socket_match : socket_matches) {
    if (socket_match.name().empty()) {
      creation_status = absl::InvalidArgumentError(
          "Transport socket name is required when using transport_socket_matcher");
      return;
    }

    const auto& socket_config = socket_match.transport_socket();
    auto& config_factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(socket_config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        socket_config, factory_context.messageValidationVisitor(), config_factory);
    auto factory_or_error = config_factory.createTransportSocketFactory(*message, factory_context);
    SET_AND_RETURN_IF_NOT_OK(factory_or_error.status(), creation_status);

    const std::string& socket_name = socket_match.name();
    auto [_, inserted] =
        transport_sockets_by_name_.try_emplace(socket_name, std::move(factory_or_error.value()));
    if (!inserted) {
      creation_status = absl::InvalidArgumentError(fmt::format(
          "Duplicate transport socket name '{}' found in transport_socket_matches", socket_name));
      return;
    }

    // Pre-generate stats at config time to avoid mutex contention in the hot path.
    matcher_stats_by_name_.emplace(socket_name, generateStats(absl::StrCat(socket_name, ".")));
  }

  // Construct matcher.
  // Create a validation visitor for the matcher.
  class ValidationVisitor
      : public Matcher::MatchTreeValidationVisitor<TransportSocketMatchingData> {
  private:
    absl::Status
    performDataInputValidation(const Matcher::DataInputFactory<TransportSocketMatchingData>&,
                               absl::string_view) override {
      return absl::OkStatus();
    }
  } validation_visitor;

  // Use the standard matcher factory following the same pattern as filter_chain_matcher.
  Matcher::MatchTreeFactory<TransportSocketMatchingData, TransportSocketActionFactoryContext>
      factory(factory_context.serverFactoryContext(), factory_context.serverFactoryContext(),
              validation_visitor);
  auto factory_cb = factory.create(transport_socket_matcher);

  // Create the matcher instance from the factory callback.
  matcher_ = factory_cb();
  creation_status = absl::OkStatus();
}

} // namespace Upstream
} // namespace Envoy
