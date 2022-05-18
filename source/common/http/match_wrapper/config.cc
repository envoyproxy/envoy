#include "source/common/http/match_wrapper/config.h"

#include "envoy/extensions/filters/common/matcher/action/v3/skip_action.pb.h"
#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

#include "absl/status/status.h"
#include <memory>

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchWrapper {

namespace {

class SkipAction : public Matcher::ActionBase<
                       envoy::extensions::filters::common::matcher::action::v3::SkipFilter> {};

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

class DelegatingStreamFilter : public Envoy::Http::StreamFilter {
public:
  using MatchDataUpdateFunc = std::function<void(Envoy::Http::Matching::HttpMatchingDataImpl&)>;

  class FilterMatchState {
  public:
    FilterMatchState(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree)
        : match_tree_(std::move(match_tree)), has_match_tree_(match_tree_ != nullptr) {}

    void evaluateMatchTree(MatchDataUpdateFunc data_update_func) {
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

    bool skipFilter() const { return skip_filter_; }

    void onStreamInfo(const StreamInfo::StreamInfo& stream_info) {
      if (has_match_tree_ && matching_data_ == nullptr) {
        matching_data_ = std::make_shared<Envoy::Http::Matching::HttpMatchingDataImpl>(
            stream_info.downstreamAddressProvider());
      }
    }
    void setBaseFilter(Envoy::Http::StreamFilterBase* base_filter) { base_filter_ = base_filter; }

  private:
    Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
    const bool has_match_tree_{};
    Envoy::Http::StreamFilterBase* base_filter_{};

    Envoy::Http::Matching::HttpMatchingDataImplSharedPtr matching_data_;
    bool match_tree_evaluated_{};
    bool skip_filter_{};
  };
  using FilterMatchStatePtr = std::unique_ptr<FilterMatchState>;

  DelegatingStreamFilter(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree,
                         Envoy::Http::StreamDecoderFilterSharedPtr decoder_filter,
                         Envoy::Http::StreamEncoderFilterSharedPtr encoder_filter)
      : match_state_(std::move(match_tree)), decoder_filter_(std::move(decoder_filter)),
        encoder_filter_(std::move(encoder_filter)) {

    ASSERT(!(decoder_filter_ == nullptr && encoder_filter_ == nullptr));

    if (encoder_filter_ != nullptr) {
      base_filter_ = encoder_filter_.get();
    } else {
      base_filter_ = decoder_filter_.get();
    }
    match_state_.setBaseFilter(base_filter_);
  }

  // Envoy::Http::StreamFilterBase
  void onStreamComplete() override { base_filter_->onStreamComplete(); }
  void onDestroy() override { base_filter_->onDestroy(); }
  void onMatchCallback(const Matcher::Action& action) override {
    base_filter_->onMatchCallback(action);
  }
  Envoy::Http::LocalErrorStatus onLocalReply(const LocalReplyData& data) override {
    return base_filter_->onLocalReply(data);
  }

  // Envoy::Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                 bool end_stream) override {
    match_state_.evaluateMatchTree([&headers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
      data.onRequestHeaders(headers);
    });
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterHeadersStatus::Continue;
    }
    return decoder_filter_->decodeHeaders(headers, end_stream);
  }
  Envoy::Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterDataStatus::Continue;
    }
    return decoder_filter_->decodeData(data, end_stream);
  }
  Envoy::Http::FilterTrailersStatus
  decodeTrailers(Envoy::Http::RequestTrailerMap& trailers) override {
    match_state_.evaluateMatchTree([&trailers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
      data.onRequestTrailers(trailers);
    });
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterTrailersStatus::Continue;
    }
    return decoder_filter_->decodeTrailers(trailers);
  }
  Envoy::Http::FilterMetadataStatus
  decodeMetadata(Envoy::Http::MetadataMap& metadata_map) override {
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterMetadataStatus::Continue;
    }
    return decoder_filter_->decodeMetadata(metadata_map);
  }
  void decodeComplete() override {
    if (match_state_.skipFilter()) {
      return;
    }
    decoder_filter_->decodeComplete();
  }
  void setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks& callbacks) override {
    match_state_.onStreamInfo(callbacks.streamInfo());
    decoder_filter_->setDecoderFilterCallbacks(callbacks);
  }

  // Envoy::Http::StreamEncoderFilter
  Envoy::Http::FilterHeadersStatus
  encode1xxHeaders(Envoy::Http::ResponseHeaderMap& headers) override {
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterHeadersStatus::Continue;
    }
    return encoder_filter_->encode1xxHeaders(headers);
  }
  Envoy::Http::FilterHeadersStatus encodeHeaders(Envoy::Http::ResponseHeaderMap& headers,
                                                 bool end_stream) override {

    match_state_.evaluateMatchTree([&headers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
      data.onResponseHeaders(headers);
    });
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterHeadersStatus::Continue;
    }
    return encoder_filter_->encodeHeaders(headers, end_stream);
  }
  Envoy::Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterDataStatus::Continue;
    }
    return encoder_filter_->encodeData(data, end_stream);
  }
  Envoy::Http::FilterTrailersStatus
  encodeTrailers(Envoy::Http::ResponseTrailerMap& trailers) override {
    match_state_.evaluateMatchTree([&trailers](Envoy::Http::Matching::HttpMatchingDataImpl& data) {
      data.onResponseTrailers(trailers);
    });
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterTrailersStatus::Continue;
    }
    return encoder_filter_->encodeTrailers(trailers);
  }
  Envoy::Http::FilterMetadataStatus
  encodeMetadata(Envoy::Http::MetadataMap& metadata_map) override {
    if (match_state_.skipFilter()) {
      return Envoy::Http::FilterMetadataStatus::Continue;
    }
    return decoder_filter_->decodeMetadata(metadata_map);
  }
  void encodeComplete() override {
    if (match_state_.skipFilter()) {
      return;
    }
    encoder_filter_->encodeComplete();
  }
  void setEncoderFilterCallbacks(Envoy::Http::StreamEncoderFilterCallbacks& callbacks) override {
    match_state_.onStreamInfo(callbacks.streamInfo());
    encoder_filter_->setEncoderFilterCallbacks(callbacks);
  }

private:
  FilterMatchState match_state_;

  Envoy::Http::StreamDecoderFilterSharedPtr decoder_filter_;
  Envoy::Http::StreamEncoderFilterSharedPtr encoder_filter_;
  Envoy::Http::StreamFilterBase* base_filter_{};
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
  void addStreamDecoderFilter(Envoy::Http::StreamDecoderFilterSharedPtr,
                              Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>) override {
  }
  void addStreamEncoderFilter(Envoy::Http::StreamEncoderFilterSharedPtr filter) override {
    auto delegating_filter =
        std::make_shared<DelegatingStreamFilter>(match_tree_, nullptr, std::move(filter));
    delegated_callbacks_.addStreamEncoderFilter(std::move(delegating_filter));
  }
  void addStreamEncoderFilter(Envoy::Http::StreamEncoderFilterSharedPtr,
                              Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>) override {
  }
  void addStreamFilter(Envoy::Http::StreamFilterSharedPtr filter) override {
    auto delegating_filter = std::make_shared<DelegatingStreamFilter>(match_tree_, filter, filter);
    delegated_callbacks_.addStreamFilter(std::move(delegating_filter));
  }

  // This method now is unnecessary because we we needn't inject match tree to the filter chain
  // manager.
  void addStreamFilter(Envoy::Http::StreamFilterSharedPtr,
                       Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>) override {}

  void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) override {
    delegated_callbacks_.addAccessLogHandler(std::move(handler));
  }

  Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks_;
  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
};
} // namespace

Envoy::Http::FilterFactoryCb MatchWrapperConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, Server::Configuration::FactoryContext& context) {

  ASSERT(proto_config.has_extension_config());
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          proto_config.extension_config());

  auto message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.extension_config().typed_config(), context.messageValidationVisitor(), factory);
  auto filter_factory = factory.createFilterFactoryFromProto(*message, prefix, context);

  MatchTreeValidationVisitor validation_visitor(*factory.matchingRequirements());

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
    DelegatingFactoryCallbacks delegated_callbacks(callbacks, match_tree);
    return filter_factory(delegated_callbacks);
  };
}

/**
 * Static registration for the match wrapper filter. @see RegisterFactory.
 * Note that we register this as a filter in order to serve as a drop in wrapper for other HTTP
 * filters. While not a real filter, by being registered as one all the code paths that look up HTTP
 * filters will look up this filter factory instead, which does the work to create and associate a
 * match tree with the underlying filter.
 */
REGISTER_FACTORY(MatchWrapperConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace MatchWrapper
} // namespace Http
} // namespace Common
} // namespace Envoy
