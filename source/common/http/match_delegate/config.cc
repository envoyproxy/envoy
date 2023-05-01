#include "source/common/http/match_delegate/config.h"

#include <memory>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchDelegate {

namespace Factory {

class SkipActionFactory
    : public Matcher::ActionFactory<Envoy::Http::Matching::HttpFilterActionContext> {
public:
  std::string name() const override { return "skip"; }
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message&,
                                                 Envoy::Http::Matching::HttpFilterActionContext&,
                                                 ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<SkipAction>(); };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::common::matcher::action::v3::SkipFilter>();
  }
};

REGISTER_FACTORY(SkipActionFactory,
                 Matcher::ActionFactory<Envoy::Http::Matching::HttpFilterActionContext>);

class MatchTreeValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Envoy::Http::HttpMatchingData> {
public:
  explicit MatchTreeValidationVisitor(
      const envoy::extensions::filters::common::dependency::v3::MatchingRequirements&
          requirements) {
    if (requirements.has_data_input_allow_list()) {
      data_input_allowlist_ = requirements.data_input_allow_list().type_url();
    }
  }
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Envoy::Http::HttpMatchingData>&,
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

struct DelegatingFactoryCallbacks : public Envoy::Http::FilterChainFactoryCallbacks {
  DelegatingFactoryCallbacks(Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks,
                             Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree)
      : delegated_callbacks_(delegated_callbacks), match_tree_(match_tree) {}

  Event::Dispatcher& dispatcher() override { return delegated_callbacks_.dispatcher(); }
  void addStreamDecoderFilter(Envoy::Http::StreamDecoderFilterSharedPtr filter) override {
    auto delegating_filter =
        std::make_shared<DelegatingStreamFilter>(match_tree_, std::move(filter), nullptr);
    delegated_callbacks_.addStreamDecoderFilter(std::move(delegating_filter));
  }
  void addStreamEncoderFilter(Envoy::Http::StreamEncoderFilterSharedPtr filter) override {
    auto delegating_filter =
        std::make_shared<DelegatingStreamFilter>(match_tree_, nullptr, std::move(filter));
    delegated_callbacks_.addStreamEncoderFilter(std::move(delegating_filter));
  }
  void addStreamFilter(Envoy::Http::StreamFilterSharedPtr filter) override {
    auto delegating_filter = std::make_shared<DelegatingStreamFilter>(match_tree_, filter, filter);
    delegated_callbacks_.addStreamFilter(std::move(delegating_filter));
  }

  void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) override {
    delegated_callbacks_.addAccessLogHandler(std::move(handler));
  }

  Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks_;
  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
};

} // namespace Factory

void DelegatingStreamFilter::FilterMatchState::evaluateMatchTree(
    MatchDataUpdateFunc data_update_func) {
  if (!has_match_tree_ || match_tree_evaluated_) {
    return;
  }
  ASSERT(matching_data_ != nullptr);
  data_update_func(*matching_data_);

  const auto match_result =
      Matcher::evaluateMatch<Envoy::Http::HttpMatchingData>(*match_tree_, *matching_data_);

  match_tree_evaluated_ = match_result.match_state_ == Matcher::MatchState::MatchComplete;

  if (match_tree_evaluated_ && match_result.result_) {
    const auto result = match_result.result_();
    if (SkipAction().typeUrl() == result->typeUrl()) {
      skip_filter_ = true;
    } else {
      ASSERT(base_filter_ != nullptr);
      base_filter_->onMatchCallback(*result);
    }
  }
}

DelegatingStreamFilter::DelegatingStreamFilter(
    Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree,
    Envoy::Http::StreamDecoderFilterSharedPtr decoder_filter,
    Envoy::Http::StreamEncoderFilterSharedPtr encoder_filter)
    : match_state_(std::move(match_tree)), decoder_filter_(std::move(decoder_filter)),
      encoder_filter_(std::move(encoder_filter)) {

  if (encoder_filter_ != nullptr) {
    base_filter_ = encoder_filter_.get();
  } else {
    base_filter_ = decoder_filter_.get();
  }
  match_state_.setBaseFilter(base_filter_);
}

Envoy::Http::FilterHeadersStatus
DelegatingStreamFilter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool end_stream) {
  getMatchState().evaluateMatchTree([&headers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onRequestHeaders(headers);
  });
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  return decoder_filter_->decodeHeaders(headers, end_stream);
}

Envoy::Http::FilterDataStatus DelegatingStreamFilter::decodeData(Buffer::Instance& data,
                                                                 bool end_stream) {
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterDataStatus::Continue;
  }
  return decoder_filter_->decodeData(data, end_stream);
}
Envoy::Http::FilterTrailersStatus
DelegatingStreamFilter::decodeTrailers(Envoy::Http::RequestTrailerMap& trailers) {
  getMatchState().evaluateMatchTree([&trailers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onRequestTrailers(trailers);
  });
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterTrailersStatus::Continue;
  }
  return decoder_filter_->decodeTrailers(trailers);
}

Envoy::Http::FilterMetadataStatus
DelegatingStreamFilter::decodeMetadata(Envoy::Http::MetadataMap& metadata_map) {
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterMetadataStatus::Continue;
  }
  return decoder_filter_->decodeMetadata(metadata_map);
}

void DelegatingStreamFilter::decodeComplete() {
  if (getMatchState().skipFilter()) {
    return;
  }
  decoder_filter_->decodeComplete();
}

void DelegatingStreamFilter::setDecoderFilterCallbacks(
    Envoy::Http::StreamDecoderFilterCallbacks& callbacks) {
  getMatchState().onStreamInfo(callbacks.streamInfo());
  decoder_callbacks_ = &callbacks;
  decoder_filter_->setDecoderFilterCallbacks(callbacks);
}

Envoy::Http::Filter1xxHeadersStatus
DelegatingStreamFilter::encode1xxHeaders(Envoy::Http::ResponseHeaderMap& headers) {
  if (getMatchState().skipFilter()) {
    return Envoy::Http::Filter1xxHeadersStatus::Continue;
  }
  return encoder_filter_->encode1xxHeaders(headers);
}

Envoy::Http::FilterHeadersStatus
DelegatingStreamFilter::encodeHeaders(Envoy::Http::ResponseHeaderMap& headers, bool end_stream) {

  getMatchState().evaluateMatchTree([&headers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onResponseHeaders(headers);
  });
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  return encoder_filter_->encodeHeaders(headers, end_stream);
}

Envoy::Http::FilterDataStatus DelegatingStreamFilter::encodeData(Buffer::Instance& data,
                                                                 bool end_stream) {
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterDataStatus::Continue;
  }
  return encoder_filter_->encodeData(data, end_stream);
}

Envoy::Http::FilterTrailersStatus
DelegatingStreamFilter::encodeTrailers(Envoy::Http::ResponseTrailerMap& trailers) {
  getMatchState().evaluateMatchTree([&trailers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onResponseTrailers(trailers);
  });
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterTrailersStatus::Continue;
  }
  return encoder_filter_->encodeTrailers(trailers);
}

Envoy::Http::FilterMetadataStatus
DelegatingStreamFilter::encodeMetadata(Envoy::Http::MetadataMap& metadata_map) {
  if (getMatchState().skipFilter()) {
    return Envoy::Http::FilterMetadataStatus::Continue;
  }
  return decoder_filter_->decodeMetadata(metadata_map);
}

void DelegatingStreamFilter::encodeComplete() {
  if (getMatchState().skipFilter()) {
    return;
  }
  encoder_filter_->encodeComplete();
}

void DelegatingStreamFilter::setEncoderFilterCallbacks(
    Envoy::Http::StreamEncoderFilterCallbacks& callbacks) {
  getMatchState().onStreamInfo(callbacks.streamInfo());
  encoder_callbacks_ = &callbacks;
  encoder_filter_->setEncoderFilterCallbacks(callbacks);
}

DelegatingStreamFilter::FilterMatchState& DelegatingStreamFilter::getMatchState() {
  const Envoy::Http::StreamFilterCallbacks* callbacks = nullptr;
  if (encoder_callbacks_ != nullptr) {
    callbacks = encoder_callbacks_;
  } else if (decoder_callbacks_ != nullptr) {
    callbacks = decoder_callbacks_;
  } else {
    return match_state_;
  }

  const auto* per_route_config =
      Envoy::Http::Utility::resolveMostSpecificPerFilterConfig<
          ExtensionWithMatcherFilterConfigPerRoute>(callbacks);

  if (per_route_config != nullptr) {
    auto per_route_match_state = per_route_config->matchState();
    if (per_route_match_state.hasMatchTree()) {
      per_route_match_state.setBaseFilter(base_filter_);
      return match_state_;
    }
  }
  return match_state_;
}

Envoy::Http::FilterFactoryCb MatchDelegateConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, Server::Configuration::FactoryContext& context) {

  ASSERT(proto_config.has_extension_config());
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          proto_config.extension_config());

  auto message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.extension_config().typed_config(), context.messageValidationVisitor(), factory);
  auto filter_factory = factory.createFilterFactoryFromProto(*message, prefix, context);

  Factory::MatchTreeValidationVisitor validation_visitor(*factory.matchingRequirements());

  Envoy::Http::Matching::HttpFilterActionContext action_context{prefix, context};
  Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData,
                            Envoy::Http::Matching::HttpFilterActionContext>
      matcher_factory(action_context, context.getServerFactoryContext(), validation_visitor);
  Matcher::MatchTreeFactoryCb<Envoy::Http::HttpMatchingData> factory_cb;
  if (proto_config.has_xds_matcher()) {
    factory_cb = matcher_factory.create(proto_config.xds_matcher());
  } else if (proto_config.has_matcher()) {
    factory_cb = matcher_factory.create(proto_config.matcher());
  } else {
    throw EnvoyException("one of `matcher` and `matcher_tree` must be set.");
  }

  if (!validation_visitor.errors().empty()) {
    // TODO(snowp): Output all violations.
    throw EnvoyException(fmt::format("requirement violation while creating match tree: {}",
                                     validation_visitor.errors()[0]));
  }

  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree = factory_cb();
  return [filter_factory, match_tree](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    Factory::DelegatingFactoryCallbacks delegating_callbacks(callbacks, match_tree);
    return filter_factory(delegating_callbacks);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
MatchDelegateConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<ExtensionWithMatcherFilterConfigPerRoute>(
      proto_config, server_context);
}

Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>
ExtensionWithMatcherFilterConfigPerRoute::createFilterMatchState(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& server_context) {
  auto requirements =
      std::make_unique<envoy::extensions::filters::common::dependency::v3::
                           MatchingRequirements>();
  Server::Configuration::FactoryContext* context =
        reinterpret_cast<Server::Configuration::FactoryContext*>(
            &server_context);
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      server_context.scope().symbolTable().toString(
          server_context.scope().prefix()),
      *context};

  Factory::MatchTreeValidationVisitor validation_visitor(*requirements);
  Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData,
                            Envoy::Http::Matching::HttpFilterActionContext>
      matcher_factory(action_context, server_context, validation_visitor);
  Matcher::MatchTreeFactoryCb<Envoy::Http::HttpMatchingData> factory_cb =
      matcher_factory.create(proto_config.xds_matcher());
  return factory_cb();
}

/**
 * Static registration for the match delegate filter. @see RegisterFactory.
 * This match delegate filter is designed as delegate of all other HTTP filters. It will create
 * and associate a match tree with the underlying filter to help the underlying filter consume
 * match result of match tree.
 */
REGISTER_FACTORY(MatchDelegateConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
