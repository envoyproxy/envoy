#pragma once

#include <memory>

#include "envoy/access_log/access_log.h"
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
        : match_tree_(std::move(match_tree)) {}

    void evaluateMatchTree(MatchDataUpdateFunc data_update_func);
    bool skipFilter() const { return skip_filter_; }
    void onStreamInfo(const StreamInfo::StreamInfo& stream_info) {
      if (matching_data_ == nullptr) {
        matching_data_ = std::make_unique<Envoy::Http::Matching::HttpMatchingDataImpl>(stream_info);
      }
    }

    // The matcher from the per route config, if available, will override the matcher from the
    // filter config.
    void setMatchTree(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) {
      match_tree_ = std::move(match_tree);
    }

    void setBaseFilter(Envoy::Http::StreamFilterBase* base_filter) { base_filter_ = base_filter; }

  private:
    Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
    Envoy::Http::StreamFilterBase* base_filter_{};

    std::unique_ptr<Envoy::Http::Matching::HttpMatchingDataImpl> matching_data_;
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

  absl::string_view filterConfigName() const override {
    return delegated_callbacks_.filterConfigName();
  }
  void setFilterConfigName(absl::string_view name) override {
    return delegated_callbacks_.setFilterConfigName(name);
  }
  OptRef<const Router::Route> route() const override { return delegated_callbacks_.route(); }
  std::optional<bool> filterDisabled(absl::string_view config_name) const override {
    return delegated_callbacks_.filterDisabled(config_name);
  }
  const StreamInfo::StreamInfo& streamInfo() const override {
    return delegated_callbacks_.streamInfo();
  }
  Envoy::Http::RequestHeaderMapOptRef requestHeaders() const override {
    return delegated_callbacks_.requestHeaders();
  }

  Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks_;
  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
};

// Lazy variant of DelegatingStreamFilter, enabled by the
// "envoy.reloadable_features.match_delegate_lazy_creation" runtime guard.
//
// Instead of constructing the wrapped (nested) filter up front, the nested filter's
// Http::FilterFactoryCb is captured and invoked lazily: the nested filter is only created the first
// time the match tree resolves to a non-skip result for the stream. This avoids the CPU/RAM cost of
// building a filter that will be skipped. A single instance is registered as both a stream filter
// and an access log handler, so the nested filter's access loggers only run when the nested filter
// was actually created (skipped streams no longer emit its access logs).
class LazyDelegatingStreamFilter : public Logger::Loggable<Logger::Id::http>,
                                   public Envoy::Http::StreamFilter,
                                   public AccessLog::Instance {
public:
  using MatchDataUpdateFunc = std::function<void(Envoy::Http::Matching::HttpMatchingDataImpl&)>;

  class FilterMatchState {
  public:
    FilterMatchState(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree)
        : match_tree_(std::move(match_tree)) {}

    void evaluateMatchTree(MatchDataUpdateFunc data_update_func);
    bool skipFilter() const { return skip_filter_; }
    void onStreamInfo(const StreamInfo::StreamInfo& stream_info) {
      if (matching_data_ == nullptr) {
        matching_data_ = std::make_unique<Envoy::Http::Matching::HttpMatchingDataImpl>(stream_info);
      }
    }

    // The matcher from the per route config, if available, will override the matcher from the
    // filter config.
    void setMatchTree(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) {
      match_tree_ = std::move(match_tree);
    }

    // Returns and clears any non-skip custom action produced by the last match tree evaluation.
    // The delegating filter dispatches this to the (lazily created) nested filter's
    // onMatchCallback. Returns nullptr if there is no pending action.
    Matcher::ActionConstSharedPtr takePendingMatchAction() {
      return std::move(pending_match_action_);
    }

  private:
    Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;

    std::unique_ptr<Envoy::Http::Matching::HttpMatchingDataImpl> matching_data_;
    Matcher::ActionConstSharedPtr pending_match_action_;
    bool match_tree_evaluated_{};
    bool skip_filter_{};
  };

  LazyDelegatingStreamFilter(Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree,
                             Envoy::Http::FilterFactoryCb filter_factory);

  // Envoy::Http::StreamFilterBase
  void onStreamComplete() override;
  void onDestroy() override;
  void onMatchCallback(const Matcher::Action& action) override;
  Envoy::Http::LocalErrorStatus onLocalReply(const LocalReplyData& data) override;

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

  // AccessLog::Instance
  // The nested filter's access loggers only run when the nested filter was actually created (i.e.
  // the match tree did not resolve to a skip), so skipped filters no longer emit access logs.
  void log(const Formatter::Context& log_context, const StreamInfo::StreamInfo& info) override;

private:
  // Collects the filters and access log handlers created by the nested filter factory when it is
  // lazily invoked. Context accessors delegate to the live stream callbacks.
  struct FilterCreationCallbacks : public Envoy::Http::FilterChainFactoryCallbacks {
    FilterCreationCallbacks(LazyDelegatingStreamFilter& parent) : parent_(parent) {}

    void addStreamDecoderFilter(Envoy::Http::StreamDecoderFilterSharedPtr filter) override;
    void addStreamEncoderFilter(Envoy::Http::StreamEncoderFilterSharedPtr filter) override;
    void addStreamFilter(Envoy::Http::StreamFilterSharedPtr filter) override;
    void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) override;

    Event::Dispatcher& dispatcher() override;
    absl::string_view filterConfigName() const override { return {}; }
    void setFilterConfigName(absl::string_view) override {}
    OptRef<const Router::Route> route() const override { return std::nullopt; }
    std::optional<bool> filterDisabled(absl::string_view) const override { return std::nullopt; }
    const StreamInfo::StreamInfo& streamInfo() const override;
    Envoy::Http::RequestHeaderMapOptRef requestHeaders() const override { return std::nullopt; }

    LazyDelegatingStreamFilter& parent_;
  };

  // Ensures the nested filter(s) have been created and wired to the current callbacks. Safe to
  // call multiple times; creation only happens once.
  void ensureFilterCreated();
  // Evaluates the match tree, and if the filter is not skipped, lazily creates the nested filter
  // and dispatches any pending custom match action to it.
  void maybeCreateAndDispatch();

  FilterMatchState match_state_;
  Envoy::Http::FilterFactoryCb filter_factory_;

  Envoy::Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Envoy::Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};

  // Lazily created nested filters. Decoder filters are invoked in insertion order; encoder filters
  // in reverse insertion order, matching Envoy's filter chain semantics. base_filters_ holds the
  // StreamFilterBase view of each created filter for lifecycle callbacks.
  std::vector<Envoy::Http::StreamDecoderFilterSharedPtr> decoder_filters_;
  std::vector<Envoy::Http::StreamEncoderFilterSharedPtr> encoder_filters_;
  std::vector<Envoy::Http::StreamFilterBase*> base_filters_;
  AccessLog::InstanceSharedPtrVector access_loggers_;
  bool filter_created_{};
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

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
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
