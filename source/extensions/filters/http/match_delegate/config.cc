#include "source/extensions/filters/http/match_delegate/config.h"

#include <memory>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"

#include "absl/status/status.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"

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
  data_update_func(*matching_data_);

  const auto match_result =
      Matcher::evaluateMatch<Envoy::Http::HttpMatchingData>(*match_tree_, *matching_data_);

  match_tree_evaluated_ = match_result.match_state_ == Matcher::MatchState::MatchComplete;

  if (match_tree_evaluated_ && match_result.result_) {
    const auto result = match_result.result_();
    if ((result == nullptr) || (SkipAction().typeUrl() == result->typeUrl())) {
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
  const auto* per_route_config =
      Envoy::Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(
          decoder_callbacks_);

  if (per_route_config != nullptr) {
    match_state_.setMatchTree(per_route_config->matchTree());
  } else {
    ENVOY_LOG(
        trace,
        "No per route config found, thus the matcher tree can not be built from per route config");
  }

  match_state_.evaluateMatchTree([&headers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onRequestHeaders(headers);
  });
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  return decoder_filter_->decodeHeaders(headers, end_stream);
}

Envoy::Http::FilterDataStatus DelegatingStreamFilter::decodeData(Buffer::Instance& data,
                                                                 bool end_stream) {
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterDataStatus::Continue;
  }
  return decoder_filter_->decodeData(data, end_stream);
}
Envoy::Http::FilterTrailersStatus
DelegatingStreamFilter::decodeTrailers(Envoy::Http::RequestTrailerMap& trailers) {
  match_state_.evaluateMatchTree([&trailers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onRequestTrailers(trailers);
  });
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterTrailersStatus::Continue;
  }
  return decoder_filter_->decodeTrailers(trailers);
}

Envoy::Http::FilterMetadataStatus
DelegatingStreamFilter::decodeMetadata(Envoy::Http::MetadataMap& metadata_map) {
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterMetadataStatus::Continue;
  }
  return decoder_filter_->decodeMetadata(metadata_map);
}

void DelegatingStreamFilter::decodeComplete() {
  if (match_state_.skipFilter()) {
    return;
  }
  decoder_filter_->decodeComplete();
}

void DelegatingStreamFilter::setDecoderFilterCallbacks(
    Envoy::Http::StreamDecoderFilterCallbacks& callbacks) {
  match_state_.onStreamInfo(callbacks.streamInfo());
  decoder_callbacks_ = &callbacks;
  decoder_filter_->setDecoderFilterCallbacks(callbacks);
}

Envoy::Http::Filter1xxHeadersStatus
DelegatingStreamFilter::encode1xxHeaders(Envoy::Http::ResponseHeaderMap& headers) {
  if (match_state_.skipFilter()) {
    return Envoy::Http::Filter1xxHeadersStatus::Continue;
  }
  return encoder_filter_->encode1xxHeaders(headers);
}

Envoy::Http::FilterHeadersStatus
DelegatingStreamFilter::encodeHeaders(Envoy::Http::ResponseHeaderMap& headers, bool end_stream) {
  match_state_.evaluateMatchTree([&headers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onResponseHeaders(headers);
  });
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  return encoder_filter_->encodeHeaders(headers, end_stream);
}

Envoy::Http::FilterDataStatus DelegatingStreamFilter::encodeData(Buffer::Instance& data,
                                                                 bool end_stream) {
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterDataStatus::Continue;
  }
  return encoder_filter_->encodeData(data, end_stream);
}

Envoy::Http::FilterTrailersStatus
DelegatingStreamFilter::encodeTrailers(Envoy::Http::ResponseTrailerMap& trailers) {
  match_state_.evaluateMatchTree([&trailers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
    data.onResponseTrailers(trailers);
  });
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterTrailersStatus::Continue;
  }
  return encoder_filter_->encodeTrailers(trailers);
}

Envoy::Http::FilterMetadataStatus
DelegatingStreamFilter::encodeMetadata(Envoy::Http::MetadataMap& metadata_map) {
  if (match_state_.skipFilter()) {
    return Envoy::Http::FilterMetadataStatus::Continue;
  }
  return decoder_filter_->decodeMetadata(metadata_map);
}

void DelegatingStreamFilter::encodeComplete() {
  if (match_state_.skipFilter()) {
    return;
  }
  encoder_filter_->encodeComplete();
}

void DelegatingStreamFilter::setEncoderFilterCallbacks(
    Envoy::Http::StreamEncoderFilterCallbacks& callbacks) {
  match_state_.onStreamInfo(callbacks.streamInfo());
  encoder_callbacks_ = &callbacks;
  encoder_filter_->setEncoderFilterCallbacks(callbacks);
}

absl::StatusOr<Envoy::Http::FilterFactoryCb> MatchDelegateConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, Server::Configuration::FactoryContext& context) {
  ASSERT(proto_config.has_extension_config());
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          proto_config.extension_config());
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = prefix,
      .factory_context_ = context,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = context.serverFactoryContext()};
  return createFilterFactory(proto_config, prefix, context.messageValidationVisitor(),
                             action_context, context, factory);
}

absl::StatusOr<Envoy::Http::FilterFactoryCb> MatchDelegateConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, Server::Configuration::UpstreamFactoryContext& context) {
  ASSERT(proto_config.has_extension_config());
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::UpstreamHttpFilterConfigFactory>(
          proto_config.extension_config());
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = false,
      .stat_prefix_ = prefix,
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = context,
      .server_factory_context_ = context.serverFactoryContext()};
  return createFilterFactory(proto_config, prefix,
                             context.serverFactoryContext().messageValidationVisitor(),
                             action_context, context, factory);
}

template <class FactoryCtx, class FilterCfgFactory>
absl::StatusOr<Envoy::Http::FilterFactoryCb> MatchDelegateConfig::createFilterFactory(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, ProtobufMessage::ValidationVisitor& validation,
    Envoy::Http::Matching::HttpFilterActionContext& action_context, FactoryCtx& context,
    FilterCfgFactory& factory) {
  auto message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.extension_config().typed_config(), validation, factory);
  auto filter_factory_or_error = factory.createFilterFactoryFromProto(*message, prefix, context);
  RETURN_IF_NOT_OK_REF(filter_factory_or_error.status());
  auto filter_factory = filter_factory_or_error.value();

  Factory::MatchTreeValidationVisitor validation_visitor(*factory.matchingRequirements());

  Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData,
                            Envoy::Http::Matching::HttpFilterActionContext>
      matcher_factory(action_context, context.serverFactoryContext(), validation_visitor);
  absl::optional<Matcher::MatchTreeFactoryCb<Envoy::Http::HttpMatchingData>> factory_cb =
      std::nullopt;
  if (proto_config.has_xds_matcher()) {
    factory_cb = matcher_factory.create(proto_config.xds_matcher());
  } else if (proto_config.has_matcher()) {
    factory_cb = matcher_factory.create(proto_config.matcher());
  }

  if (!validation_visitor.errors().empty()) {
    // TODO(snowp): Output all violations.
    return absl::InvalidArgumentError(fmt::format(
        "requirement violation while creating match tree: {}", validation_visitor.errors()[0]));
  }

  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree = nullptr;

  if (factory_cb.has_value()) {
    match_tree = factory_cb.value()();
  }

  return [filter_factory, match_tree](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    Factory::DelegatingFactoryCallbacks delegating_callbacks(callbacks, match_tree);
    return filter_factory(delegating_callbacks);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
MatchDelegateConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& server_context,
    ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config, server_context);
}

Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>
FilterConfigPerRoute::createFilterMatchTree(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& server_context) {
  auto requirements =
      std::make_unique<envoy::extensions::filters::common::dependency::v3::MatchingRequirements>();
  requirements->mutable_data_input_allow_list()->add_type_url(TypeUtil::descriptorFullNameToTypeUrl(
      envoy::type::matcher::v3::HttpRequestHeaderMatchInput::default_instance().GetTypeName()));
  requirements->mutable_data_input_allow_list()->add_type_url(TypeUtil::descriptorFullNameToTypeUrl(
      xds::type::matcher::v3::HttpAttributesCelMatchInput::default_instance().GetTypeName()));
  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ = fmt::format("http.{}.", server_context.scope().symbolTable().toString(
                                                  server_context.scope().prefix())),
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = server_context};

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
REGISTER_FACTORY(UpstreamMatchDelegateConfig,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
