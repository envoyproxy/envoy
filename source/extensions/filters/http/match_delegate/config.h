#pragma once

#include <memory>

#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/extensions/filters/common/matcher/action/v3/skip_action.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/filter_config.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchDelegate {

class SkipAction : public Matcher::ActionBase<
                       envoy::extensions::filters::common::matcher::action::v3::SkipFilter> {};

class DelegatingStreamFilter : public Logger::Loggable<Logger::Id::http>,
                               public Envoy::Http::StreamFilter {
public:
  using MatchDataUpdateFunc = std::function<void(Envoy::Http::Matching::HttpMatchingDataImpl&)>;

  class FilterMatchState {
  public:
    FilterMatchState(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree)
        : match_tree_(std::move(match_tree)), has_match_tree_(match_tree_ != nullptr) {}

    void evaluateMatchTree(MatchDataUpdateFunc data_update_func);
    bool skipFilter() const { return skip_filter_; }
    void onStreamInfo(const StreamInfo::StreamInfo& stream_info) {
      if (matching_data_ == nullptr) {
        matching_data_ = std::make_shared<Envoy::Http::Matching::HttpMatchingDataImpl>(stream_info);
      }
    }

    // The matcher from the per route config, if available, will override the matcher from the
    // filter config.
    void setMatchTree(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) {
      match_tree_ = std::move(match_tree);
      has_match_tree_ = match_tree_ != nullptr;
    }

    void setBaseFilter(Envoy::Http::StreamFilterBase* base_filter) { base_filter_ = base_filter; }

  private:
    Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
    bool has_match_tree_{};
    Envoy::Http::StreamFilterBase* base_filter_{};

    Envoy::Http::Matching::HttpMatchingDataImplSharedPtr matching_data_;
    bool match_tree_evaluated_{};
    bool skip_filter_{};
  };

  DelegatingStreamFilter(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree,
                         Envoy::Http::StreamDecoderFilterSharedPtr decoder_filter,
                         Envoy::Http::StreamEncoderFilterSharedPtr encoder_filter);

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
                                                 bool end_stream) override;
  Envoy::Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Envoy::Http::FilterTrailersStatus
  decodeTrailers(Envoy::Http::RequestTrailerMap& trailers) override;

  Envoy::Http::FilterMetadataStatus decodeMetadata(Envoy::Http::MetadataMap& metadata_map) override;
  void decodeComplete() override;
  void setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Envoy::Http::StreamEncoderFilter
  Envoy::Http::Filter1xxHeadersStatus
  encode1xxHeaders(Envoy::Http::ResponseHeaderMap& headers) override;
  Envoy::Http::FilterHeadersStatus encodeHeaders(Envoy::Http::ResponseHeaderMap& headers,
                                                 bool end_stream) override;
  Envoy::Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Envoy::Http::FilterTrailersStatus
  encodeTrailers(Envoy::Http::ResponseTrailerMap& trailers) override;
  Envoy::Http::FilterMetadataStatus encodeMetadata(Envoy::Http::MetadataMap& metadata_map) override;
  void encodeComplete() override;
  void setEncoderFilterCallbacks(Envoy::Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  FilterMatchState match_state_;

  Envoy::Http::StreamDecoderFilterSharedPtr decoder_filter_;
  Envoy::Http::StreamEncoderFilterSharedPtr encoder_filter_;
  Envoy::Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Envoy::Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Envoy::Http::StreamFilterBase* base_filter_{};
};

class MatchDelegateConfig
    : public Extensions::HttpFilters::Common::CommonFactoryBase<
          envoy::extensions::common::matching::v3::ExtensionWithMatcher,
          envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute>,
      public Server::Configuration::NamedHttpFilterConfigFactory,
      public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  MatchDelegateConfig()
      : CommonFactoryBase<envoy::extensions::common::matching::v3::ExtensionWithMatcher,
                          envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute>(
            "envoy.filters.http.match_delegate") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::common::matching::v3::ExtensionWithMatcher&>(
            proto_config, context.messageValidationVisitor()),
        stats_prefix, context);
  }

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::UpstreamFactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::common::matching::v3::ExtensionWithMatcher&&>(
            proto_config, context.serverFactoryContext().messageValidationVisitor()),
        stats_prefix, context);
  }

private:
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
      const std::string& prefix, Server::Configuration::FactoryContext& context);
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
      const std::string& prefix, Server::Configuration::UpstreamFactoryContext& context);

  template <class FactoryCtx, class FilterCfgFactory>
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
      const std::string& prefix, ProtobufMessage::ValidationVisitor& validation,
      Envoy::Http::Matching::HttpFilterActionContext& action_context, FactoryCtx& context,
      FilterCfgFactory& factory);

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validation) override;
};

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& server_context)
      : match_tree_(createFilterMatchTree(proto_config, server_context)) {}

  const Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>& matchTree() const {
    return match_tree_;
  }

private:
  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> createFilterMatchTree(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcherPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& server_context);
  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
};

using UpstreamMatchDelegateConfig = MatchDelegateConfig;

DECLARE_FACTORY(MatchDelegateConfig);
DECLARE_FACTORY(UpstreamMatchDelegateConfig);

namespace Factory {
DECLARE_FACTORY(SkipActionFactory);
} // namespace Factory

} // namespace MatchDelegate
} // namespace Http
} // namespace Common
} // namespace Envoy
