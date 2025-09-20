#include "source/common/upstream/transport_socket_match_impl.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/common/matcher/exact_map_matcher.h"
#include "source/common/matcher/matcher.h"
#include "source/common/matcher/prefix_map_matcher.h"

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
    const xds::type::matcher::v3::Matcher* transport_socket_matcher,
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

TransportSocketMatcherImpl::TransportSocketMatcherImpl(
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    const xds::type::matcher::v3::Matcher* transport_socket_matcher,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Network::UpstreamTransportSocketFactoryPtr& default_factory, Stats::Scope& stats_scope,
    absl::Status& creation_status)
    : stats_scope_(stats_scope),
      default_match_("default", std::move(default_factory), generateStats("default")) {
  // Setup transport socket matcher if provided.
  if (transport_socket_matcher) {
    setupTransportSocketMatcher(transport_socket_matcher, socket_matches, factory_context,
                                creation_status);
    if (!creation_status.ok()) {
      return;
    }
  } else {
    // Fall back to the legacy metadata-based matching.
    for (const auto& socket_match : socket_matches) {
      const auto& socket_config = socket_match.transport_socket();
      auto& config_factory = Config::Utility::getAndCheckFactory<
          Server::Configuration::UpstreamTransportSocketConfigFactory>(socket_config);
      ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
          socket_config, factory_context.messageValidationVisitor(), config_factory);
      auto factory_or_error =
          config_factory.createTransportSocketFactory(*message, factory_context);
      SET_AND_RETURN_IF_NOT_OK(factory_or_error.status(), creation_status);
      FactoryMatch factory_match(socket_match.name(), std::move(factory_or_error.value()),
                                 generateStats(absl::StrCat(socket_match.name(), ".")));
      for (const auto& kv : socket_match.match().fields()) {
        factory_match.label_set.emplace_back(kv.first, kv.second);
      }
      matches_.emplace_back(std::move(factory_match));
    }
  }
}

TransportSocketMatchStats
TransportSocketMatcherImpl::generateStats(const std::string& prefix) const {
  return {ALL_TRANSPORT_SOCKET_MATCH_STATS(POOL_COUNTER_PREFIX(stats_scope_, prefix))};
}

TransportSocketMatcher::MatchData TransportSocketMatcherImpl::resolve(
    const envoy::config::core::v3::Metadata* endpoint_metadata,
    const envoy::config::core::v3::Metadata* locality_metadata) const {
  // If matcher is available, use matcher-based resolution.
  if (matcher_) {
    return resolveUsingMatcher(endpoint_metadata, locality_metadata);
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

TransportSocketMatcher::MatchData
TransportSocketMatcherImpl::resolve(const envoy::config::core::v3::Metadata* endpoint_metadata,
                                    const envoy::config::core::v3::Metadata* locality_metadata,
                                    const Network::Address::Instance* local_address,
                                    const Network::Address::Instance* remote_address,
                                    const Network::ConnectionInfoProvider* connection_info,
                                    const StreamInfo::FilterState* filter_state,
                                    absl::string_view server_name,
                                    const std::vector<std::string>* application_protocols) const {
  // If matcher is available, use matcher-based resolution with full network context.
  if (matcher_) {
    TransportSocketMatchingData data(endpoint_metadata, locality_metadata, local_address,
                                     remote_address, connection_info, filter_state, server_name,
                                     application_protocols);
    auto match_result = Matcher::evaluateMatch(*matcher_, data);
    if (match_result.isMatch()) {
      const auto action = match_result.action();
      if (action) {
        const auto& name_action = action->getTyped<TransportSocketNameAction>();
        const std::string& transport_socket_name = name_action.name();
        const auto factory_it = transport_sockets_by_name_.find(transport_socket_name);
        if (factory_it != transport_sockets_by_name_.end()) {
          auto it_stats = matcher_stats_by_name_.find(transport_socket_name);
          if (it_stats == matcher_stats_by_name_.end()) {
            it_stats = matcher_stats_by_name_
                           .emplace(transport_socket_name,
                                    generateStats(absl::StrCat(transport_socket_name, ".")))
                           .first;
          }
          return {*factory_it->second, it_stats->second, transport_socket_name};
        }
        ENVOY_LOG(warn, "Transport socket '{}' not found in configuration, using default",
                  transport_socket_name);
      }
    }
    // Fall back to default if no match or action found.
    return {*default_match_.factory, default_match_.stats, default_match_.name};
  }

  // Fall back to legacy metadata-based matching if no matcher is configured.
  return resolve(endpoint_metadata, locality_metadata);
}

TransportSocketMatcher::MatchData TransportSocketMatcherImpl::resolveUsingMatcher(
    const envoy::config::core::v3::Metadata* endpoint_metadata,
    const envoy::config::core::v3::Metadata* locality_metadata) const {
  TransportSocketMatchingData data(endpoint_metadata, locality_metadata);
  auto on_match = Matcher::evaluateMatch(*matcher_, data);
  if (on_match.isMatch()) {
    const auto action = on_match.action();
    if (action) {
      const auto& name_action = action->getTyped<TransportSocketNameAction>();
      const std::string& transport_socket_name = name_action.name();
      const auto it = transport_sockets_by_name_.find(transport_socket_name);
      if (it != transport_sockets_by_name_.end()) {
        auto it_stats = matcher_stats_by_name_.find(transport_socket_name);
        if (it_stats == matcher_stats_by_name_.end()) {
          it_stats = matcher_stats_by_name_
                         .emplace(transport_socket_name,
                                  generateStats(absl::StrCat(transport_socket_name, ".")))
                         .first;
        }
        return {*it->second, it_stats->second, transport_socket_name};
      }
      ENVOY_LOG(warn, "Transport socket '{}' not found, using default", transport_socket_name);
    }
  }

  // Fall back to default if no match or action found.
  return {*default_match_.factory, default_match_.stats, default_match_.name};
}

void TransportSocketMatcherImpl::setupTransportSocketMatcher(
    const xds::type::matcher::v3::Matcher* transport_socket_matcher,
    const Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster::TransportSocketMatch>&
        socket_matches,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    absl::Status& creation_status) {
  // Build the transport socket factories by name.
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

    auto [_, inserted] = transport_sockets_by_name_.try_emplace(
        socket_match.name(), std::move(factory_or_error.value()));
    if (!inserted) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("Duplicate transport socket name '{}' found in transport_socket_matches",
                      socket_match.name()));
      return;
    }
  }

  // Construct matcher if present in the cluster configuration.
  if (transport_socket_matcher != nullptr) {
    // Create a custom match tree factory that uses direct socket name extraction
    // instead of the standard action factory registry lookup.
    class TransportSocketMatchTreeFactory {
    public:
      TransportSocketMatchTreeFactory(
          Server::Configuration::ServerFactoryContext& server_context,
          Matcher::MatchTreeValidationVisitor<TransportSocketMatchingData>& validation_visitor,
          absl::Status& status)
          : match_input_factory_(server_context.messageValidationVisitor(), validation_visitor),
            status_(status) {}

      std::unique_ptr<Matcher::MatchTree<TransportSocketMatchingData>>
      create(const xds::type::matcher::v3::Matcher& matcher) {
        return createMatchTree(matcher);
      }

    private:
      std::unique_ptr<Matcher::MatchTree<TransportSocketMatchingData>>
      createMatchTree(const xds::type::matcher::v3::Matcher& matcher) {
        switch (matcher.matcher_type_case()) {
        case xds::type::matcher::v3::Matcher::kMatcherTree: {
          auto data_input = match_input_factory_.createDataInput(matcher.matcher_tree().input());

          switch (matcher.matcher_tree().tree_type_case()) {
          case xds::type::matcher::v3::Matcher::MatcherTree::kExactMatchMap: {
            auto matcher_or_error = Matcher::ExactMapMatcher<TransportSocketMatchingData>::create(
                data_input(), absl::nullopt);
            if (!matcher_or_error.ok()) {
              status_ = matcher_or_error.status();
              return nullptr;
            }
            auto map_matcher = std::move(*matcher_or_error);

            for (const auto& entry : matcher.matcher_tree().exact_match_map().map()) {
              if (entry.second.has_action()) {
                std::string socket_name = extractSocketName(entry.second.action());
                if (socket_name.empty()) {
                  // Invalid or unsupported action. We skip this child to avoid implicit fallback.
                  continue;
                }
                auto action = std::make_shared<TransportSocketNameAction>(socket_name);
                Matcher::OnMatch<TransportSocketMatchingData> on_match{action, nullptr, false};
                map_matcher->addChild(entry.first, std::move(on_match));
              }
            }
            return map_matcher;
          }
          case xds::type::matcher::v3::Matcher::MatcherTree::kPrefixMatchMap: {
            auto matcher_or_error = Matcher::PrefixMapMatcher<TransportSocketMatchingData>::create(
                data_input(), absl::nullopt);
            if (!matcher_or_error.ok()) {
              status_ = matcher_or_error.status();
              return nullptr;
            }
            auto map_matcher = std::move(*matcher_or_error);

            for (const auto& entry : matcher.matcher_tree().prefix_match_map().map()) {
              if (entry.second.has_action()) {
                std::string socket_name = extractSocketName(entry.second.action());
                if (socket_name.empty()) {
                  // Invalid or unsupported action. We skip this child to avoid implicit fallback.
                  continue;
                }
                auto action = std::make_shared<TransportSocketNameAction>(socket_name);
                Matcher::OnMatch<TransportSocketMatchingData> on_match{action, nullptr, false};
                map_matcher->addChild(entry.first, std::move(on_match));
              }
            }
            return map_matcher;
          }
          case xds::type::matcher::v3::Matcher::MatcherTree::TREE_TYPE_NOT_SET:
            PANIC("unexpected matcher type");
          case xds::type::matcher::v3::Matcher::MatcherTree::kCustomMatch:
            status_ = absl::InvalidArgumentError(
                "Custom match not supported for transport socket matching");
            return nullptr;
          }
          break;
        }
        case xds::type::matcher::v3::Matcher::kMatcherList:
          status_ = absl::InvalidArgumentError(
              "Matcher list not yet supported for transport socket matching");
          return nullptr;
        case xds::type::matcher::v3::Matcher::MATCHER_TYPE_NOT_SET:
          return std::make_unique<Matcher::AnyMatcher<TransportSocketMatchingData>>(absl::nullopt);
        }
        PANIC_DUE_TO_CORRUPT_ENUM;
      }

      std::string extractSocketName(const ::xds::core::v3::TypedExtensionConfig& action) {
        if (action.name() != "transport-socket-name" || !action.has_typed_config()) {
          return std::string();
        }
        Protobuf::StringValue string_config;
        const absl::Status status = MessageUtil::unpackTo(action.typed_config(), string_config);
        if (!status.ok()) {
          return std::string();
        }
        return string_config.value();
      }

      Matcher::MatchInputFactory<TransportSocketMatchingData> match_input_factory_;
      absl::Status& status_;
    };

    // Validation visitor that implements the required abstract method.
    class ValidationVisitor
        : public Matcher::MatchTreeValidationVisitor<TransportSocketMatchingData> {
    private:
      absl::Status
      performDataInputValidation(const Matcher::DataInputFactory<TransportSocketMatchingData>&,
                                 absl::string_view) override {
        return absl::OkStatus();
      }
    } validation_visitor;

    absl::Status tree_status;
    TransportSocketMatchTreeFactory factory(factory_context.serverFactoryContext(),
                                            validation_visitor, tree_status);
    matcher_ = factory.create(*transport_socket_matcher);
    if (!tree_status.ok()) {
      creation_status = tree_status;
      return;
    }
  }
  creation_status = absl::OkStatus();
}

// Register action factory.
REGISTER_FACTORY(TransportSocketNameActionFactory,
                 Matcher::ActionFactory<TransportSocketMatchingData>);

} // namespace Upstream
} // namespace Envoy
