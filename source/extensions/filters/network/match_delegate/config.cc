#include "source/extensions/filters/network/match_delegate/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MatchDelegate {

namespace Factory {

class SkipActionFactory : public Matcher::ActionFactory<NetworkFilterActionContext> {
public:
  std::string name() const override { return "skip"; }
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message&,
                                                 NetworkFilterActionContext&,
                                                 ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<SkipAction>(); };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::common::matcher::action::v3::SkipFilter>();
  }
};

class MatchTreeValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Envoy::Network::MatchingData> {
public:
  explicit MatchTreeValidationVisitor(
      const envoy::extensions::filters::common::dependency::v3::MatchingRequirements&
          requirements) {
    if (requirements.has_data_input_allow_list()) {
      data_input_allowlist_ = requirements.data_input_allow_list().type_url();
    }
  }
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Envoy::Network::MatchingData>&,
                             absl::string_view type_url) override {
    if (!data_input_allowlist_) {
      return absl::OkStatus();
    }

    if (std::find(data_input_allowlist_->begin(), data_input_allowlist_->end(), type_url) !=
        data_input_allowlist_->end()) {
      return absl::OkStatus();
    }

    return absl::InvalidArgumentError(
        fmt::format("data input typeUrl {} not permitted according to allowlist", type_url));
  }

private:
  absl::optional<Protobuf::RepeatedPtrField<std::string>> data_input_allowlist_;
};

REGISTER_FACTORY(SkipActionFactory, Matcher::ActionFactory<NetworkFilterActionContext>);

DelegatingNetworkFilterManager::DelegatingNetworkFilterManager(
    Envoy::Network::FilterManager& filter_manager,
    Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree)
    : filter_manager_(filter_manager), match_tree_(match_tree) {}

void DelegatingNetworkFilterManager::addReadFilter(Envoy::Network::ReadFilterSharedPtr filter) {
  auto delegating_filter =
      std::make_shared<DelegatingNetworkFilter>(match_tree_, std::move(filter), nullptr);
  filter_manager_.addReadFilter(std::move(delegating_filter));
}

void DelegatingNetworkFilterManager::addWriteFilter(Envoy::Network::WriteFilterSharedPtr filter) {
  auto delegating_filter =
      std::make_shared<DelegatingNetworkFilter>(match_tree_, nullptr, std::move(filter));
  filter_manager_.addWriteFilter(std::move(delegating_filter));
}

void DelegatingNetworkFilterManager::addFilter(Envoy::Network::FilterSharedPtr filter) {
  auto delegating_filter = std::make_shared<DelegatingNetworkFilter>(match_tree_, filter, filter);
  filter_manager_.addFilter(std::move(delegating_filter));
}

void DelegatingNetworkFilterManager::removeReadFilter(Envoy::Network::ReadFilterSharedPtr) {}

bool DelegatingNetworkFilterManager::initializeReadFilters() { return false; }

} // namespace Factory

void DelegatingNetworkFilter::FilterMatchState::evaluateMatchTree() {
  if (match_tree_evaluated_) {
    return;
  }

  // If no match tree is set, interpret as a skip.
  if (!has_match_tree_) {
    skip_filter_ = true;
    match_tree_evaluated_ = true;
    return;
  }

  ASSERT(matching_data_ != nullptr);
  const auto match_result =
      Matcher::evaluateMatch<Envoy::Network::MatchingData>(*match_tree_, *matching_data_);

  match_tree_evaluated_ = match_result.match_state_ == Matcher::MatchState::MatchComplete;

  if (match_tree_evaluated_ && match_result.result_) {
    const auto result = match_result.result_();
    if ((result == nullptr) || (SkipAction().typeUrl() == result->typeUrl())) {
      skip_filter_ = true;
    } else {
      // TODO(botengyao) this would be similar to `base_filter_->onMatchCallback(*result);`
      // for Network::Filter, and it is a composite filter typically.
      ENVOY_LOG(warn,
                "Network filter match result {}, and it is not supported. The configured"
                " filter will be used.",
                result->typeUrl());
    }
  }
}

DelegatingNetworkFilter::DelegatingNetworkFilter(
    Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree,
    Envoy::Network::ReadFilterSharedPtr read_filter,
    Envoy::Network::WriteFilterSharedPtr write_filter)
    : match_state_(std::move(match_tree)), read_filter_(std::move(read_filter)),
      write_filter_(std::move(write_filter)) {}

Envoy::Network::FilterStatus DelegatingNetworkFilter::onNewConnection() {
  if (read_callbacks_) {
    match_state_.onConnection(read_callbacks_->socket(),
                              read_callbacks_->connection().streamInfo());
  }

  match_state_.evaluateMatchTree();

  if (match_state_.skipFilter()) {
    return Envoy::Network::FilterStatus::Continue;
  }
  return read_filter_->onNewConnection();
}

Envoy::Network::FilterStatus DelegatingNetworkFilter::onData(Buffer::Instance& data,
                                                             bool end_stream) {
  if (match_state_.skipFilter()) {
    return Envoy::Network::FilterStatus::Continue;
  }

  return read_filter_->onData(data, end_stream);
}

void DelegatingNetworkFilter::initializeReadFilterCallbacks(
    Envoy::Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_filter_->initializeReadFilterCallbacks(callbacks);
}

Envoy::Network::FilterStatus DelegatingNetworkFilter::onWrite(Buffer::Instance& data,
                                                              bool end_stream) {
  if (match_state_.skipFilter()) {
    return Envoy::Network::FilterStatus::Continue;
  }

  return write_filter_->onWrite(data, end_stream);
}

void DelegatingNetworkFilter::initializeWriteFilterCallbacks(
    Envoy::Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
  write_filter_->initializeWriteFilterCallbacks(callbacks);
}

Envoy::Network::FilterFactoryCb MatchDelegateConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(proto_config.has_extension_config());
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedNetworkFilterConfigFactory>(
          proto_config.extension_config());

  return createFilterFactory(proto_config, context.messageValidationVisitor(),
                             context.serverFactoryContext(), context, factory);
}

Envoy::Network::FilterFactoryCb MatchDelegateConfig::createFilterFactory(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    ProtobufMessage::ValidationVisitor& validation, NetworkFilterActionContext& action_context,
    Server::Configuration::FactoryContext& context,
    Server::Configuration::NamedNetworkFilterConfigFactory& factory) {
  auto message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.extension_config().typed_config(), validation, factory);
  auto filter_factory_or_error = factory.createFilterFactoryFromProto(*message, context);
  THROW_IF_NOT_OK(filter_factory_or_error.status());
  auto filter_factory = filter_factory_or_error.value();

  Factory::MatchTreeValidationVisitor validation_visitor(*factory.matchingRequirements());

  Matcher::MatchTreeFactory<Envoy::Network::MatchingData, NetworkFilterActionContext>
      matcher_factory(action_context, context.serverFactoryContext(), validation_visitor);
  absl::optional<Matcher::MatchTreeFactoryCb<Envoy::Network::MatchingData>> factory_cb =
      std::nullopt;

  if (proto_config.has_xds_matcher()) {
    factory_cb = matcher_factory.create(proto_config.xds_matcher());
  }

  if (!validation_visitor.errors().empty()) {
    throw EnvoyException(fmt::format("requirement violation while creating match tree: {}",
                                     validation_visitor.errors()[0]));
  }

  Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree = nullptr;
  if (factory_cb.has_value()) {
    match_tree = factory_cb.value()();
  }

  return [filter_factory, match_tree](Envoy::Network::FilterManager& filter_manager) -> void {
    Factory::DelegatingNetworkFilterManager delegating_manager(filter_manager, match_tree);
    return filter_factory(delegating_manager);
  };
}

/**
 * Static registration for the network match delegate filter.
 */
REGISTER_FACTORY(MatchDelegateConfig, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace MatchDelegate
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
