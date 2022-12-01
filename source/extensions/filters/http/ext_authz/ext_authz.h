#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_http_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * All stats for the Ext Authz filter. @see stats_macros.h
 */

#define ALL_EXT_AUTHZ_FILTER_STATS(COUNTER)                                                        \
  COUNTER(ok)                                                                                      \
  COUNTER(denied)                                                                                  \
  COUNTER(error)                                                                                   \
  COUNTER(disabled)                                                                                \
  COUNTER(failure_mode_allowed)

/**
 * Wrapper struct for ext_authz filter stats. @see stats_macros.h
 */
struct ExtAuthzFilterStats {
  ALL_EXT_AUTHZ_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the External Authorization (ext_authz) filter.
 */
class FilterConfig {
  using LabelsMap = Protobuf::Map<std::string, std::string>;

public:
  FilterConfig(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& config,
               Stats::Scope& scope, Runtime::Loader& runtime, Http::Context& http_context,
               const std::string& stats_prefix, envoy::config::bootstrap::v3::Bootstrap& bootstrap)
      : allow_partial_message_(config.with_request_body().allow_partial_message()),
        failure_mode_allow_(config.failure_mode_allow()),
        clear_route_cache_(config.clear_route_cache()),
        max_request_bytes_(config.with_request_body().max_request_bytes()),
        pack_as_bytes_(config.with_request_body().pack_as_bytes()),
        status_on_error_(toErrorCode(config.status_on_error().code())), scope_(scope),
        runtime_(runtime), http_context_(http_context),
        filter_enabled_(config.has_filter_enabled()
                            ? absl::optional<Runtime::FractionalPercent>(
                                  Runtime::FractionalPercent(config.filter_enabled(), runtime_))
                            : absl::nullopt),
        filter_enabled_metadata_(
            config.has_filter_enabled_metadata()
                ? absl::optional<Matchers::MetadataMatcher>(config.filter_enabled_metadata())
                : absl::nullopt),
        deny_at_disable_(config.has_deny_at_disable()
                             ? absl::optional<Runtime::FeatureFlag>(
                                   Runtime::FeatureFlag(config.deny_at_disable(), runtime_))
                             : absl::nullopt),
        pool_(scope_.symbolTable()),
        metadata_context_namespaces_(config.metadata_context_namespaces().begin(),
                                     config.metadata_context_namespaces().end()),
        typed_metadata_context_namespaces_(config.typed_metadata_context_namespaces().begin(),
                                           config.typed_metadata_context_namespaces().end()),
        include_peer_certificate_(config.include_peer_certificate()),
        stats_(generateStats(stats_prefix, config.stat_prefix(), scope)),
        ext_authz_ok_(pool_.add(createPoolStatName(config.stat_prefix(), "ok"))),
        ext_authz_denied_(pool_.add(createPoolStatName(config.stat_prefix(), "denied"))),
        ext_authz_error_(pool_.add(createPoolStatName(config.stat_prefix(), "error"))),
        ext_authz_failure_mode_allowed_(
            pool_.add(createPoolStatName(config.stat_prefix(), "failure_mode_allowed"))) {
    auto labels_key_it =
        bootstrap.node().metadata().fields().find(config.bootstrap_metadata_labels_key());
    if (labels_key_it != bootstrap.node().metadata().fields().end()) {
      for (const auto& labels_it : labels_key_it->second.struct_value().fields()) {
        destination_labels_[labels_it.first] = labels_it.second.string_value();
      }
    }
  }

  bool allowPartialMessage() const { return allow_partial_message_; }

  bool withRequestBody() const { return max_request_bytes_ > 0; }

  bool failureModeAllow() const { return failure_mode_allow_; }

  bool clearRouteCache() const { return clear_route_cache_; }

  uint32_t maxRequestBytes() const { return max_request_bytes_; }

  bool packAsBytes() const { return pack_as_bytes_; }

  Http::Code statusOnError() const { return status_on_error_; }

  bool filterEnabled(const envoy::config::core::v3::Metadata& metadata) {
    const bool enabled = filter_enabled_.has_value() ? filter_enabled_->enabled() : true;
    const bool enabled_metadata =
        filter_enabled_metadata_.has_value() ? filter_enabled_metadata_->match(metadata) : true;
    return enabled && enabled_metadata;
  }

  bool denyAtDisable() {
    return deny_at_disable_.has_value() ? deny_at_disable_->enabled() : false;
  }

  Stats::Scope& scope() { return scope_; }

  Http::Context& httpContext() { return http_context_; }

  const std::vector<std::string>& metadataContextNamespaces() {
    return metadata_context_namespaces_;
  }

  const std::vector<std::string>& typedMetadataContextNamespaces() {
    return typed_metadata_context_namespaces_;
  }

  const ExtAuthzFilterStats& stats() const { return stats_; }

  void incCounter(Stats::Scope& scope, Stats::StatName name) {
    scope.counterFromStatName(name).inc();
  }

  bool includePeerCertificate() const { return include_peer_certificate_; }
  const LabelsMap& destinationLabels() const { return destination_labels_; }

private:
  static Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Http::Code>(status);
    if (code >= Http::Code::Continue && code <= Http::Code::NetworkAuthenticationRequired) {
      return code;
    }
    return Http::Code::Forbidden;
  }

  ExtAuthzFilterStats generateStats(const std::string& prefix,
                                    const std::string& filter_stats_prefix, Stats::Scope& scope) {
    const std::string final_prefix = absl::StrCat(prefix, "ext_authz.", filter_stats_prefix);
    return {ALL_EXT_AUTHZ_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  // This generates ext_authz.<optional filter_stats_prefix>.name, for example: ext_authz.waf.ok
  // when filter_stats_prefix is "waf", and ext_authz.ok when filter_stats_prefix is empty.
  const std::string createPoolStatName(const std::string& filter_stats_prefix,
                                       const std::string& name) {
    return absl::StrCat("ext_authz",
                        filter_stats_prefix.empty() ? EMPTY_STRING
                                                    : absl::StrCat(".", filter_stats_prefix),
                        ".", name);
  }

  const bool allow_partial_message_;
  const bool failure_mode_allow_;
  const bool clear_route_cache_;
  const uint32_t max_request_bytes_;
  const bool pack_as_bytes_;
  const Http::Code status_on_error_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Http::Context& http_context_;
  LabelsMap destination_labels_;

  const absl::optional<Runtime::FractionalPercent> filter_enabled_;
  const absl::optional<Matchers::MetadataMatcher> filter_enabled_metadata_;
  const absl::optional<Runtime::FeatureFlag> deny_at_disable_;

  // TODO(nezdolik): stop using pool as part of deprecating cluster scope stats.
  Stats::StatNamePool pool_;

  const std::vector<std::string> metadata_context_namespaces_;
  const std::vector<std::string> typed_metadata_context_namespaces_;

  const bool include_peer_certificate_;

  // The stats for the filter.
  ExtAuthzFilterStats stats_;

public:
  // TODO(nezdolik): deprecate cluster scope stats counters in favor of filter scope stats
  // (ExtAuthzFilterStats stats_).
  const Stats::StatName ext_authz_ok_;
  const Stats::StatName ext_authz_denied_;
  const Stats::StatName ext_authz_error_;
  const Stats::StatName ext_authz_failure_mode_allowed_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Per route settings for ExtAuth. Allows customizing the CheckRequest on a
 * virtualhost\route\weighted cluster level.
 */
class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  using ContextExtensionsMap = Protobuf::Map<std::string, std::string>;

  FilterConfigPerRoute(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute& config)
      : context_extensions_(config.has_check_settings()
                                ? config.check_settings().context_extensions()
                                : ContextExtensionsMap()),
        disable_request_body_buffering_(config.has_check_settings() &&
                                        config.check_settings().disable_request_body_buffering()),
        disabled_(config.disabled()) {}

  void merge(const FilterConfigPerRoute& other);

  /**
   * @return Context extensions to add to the CheckRequest.
   */
  const ContextExtensionsMap& contextExtensions() const { return context_extensions_; }
  // Allow moving the context extensions out of this object.
  ContextExtensionsMap&& takeContextExtensions() { return std::move(context_extensions_); }

  bool disabled() const { return disabled_; }

  bool disableRequestBodyBuffering() const { return disable_request_body_buffering_; }

private:
  // We save the context extensions as a protobuf map instead of an std::map as this allows us to
  // move it to the CheckRequest, thus avoiding a copy that would incur by converting it.
  ContextExtensionsMap context_extensions_;
  bool disable_request_body_buffering_;
  bool disabled_;
};

/**
 * HTTP ext_authz filter. Depending on the route configuration, this filter calls the global
 * ext_authz service before allowing further filter iteration.
 */
class Filter : public Logger::Loggable<Logger::Id::ext_authz>,
               public Http::StreamFilter,
               public Filters::Common::ExtAuthz::RequestCallbacks {
public:
  Filter(const FilterConfigSharedPtr& config, Filters::Common::ExtAuthz::ClientPtr&& client)
      : config_(config), client_(std::move(client)), stats_(config->stats()) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // ExtAuthz::RequestCallbacks
  void onComplete(Filters::Common::ExtAuthz::ResponsePtr&&) override;

private:
  absl::optional<MonotonicTime> start_time_;
  void addResponseHeaders(Http::HeaderMap& header_map, const Http::HeaderVector& headers);
  void initiateCall(const Http::RequestHeaderMap& headers);
  void continueDecoding();
  bool isBufferFull() const;

  // This holds a set of flags defined in per-route configuration.
  struct PerRouteFlags {
    const bool skip_check_;
    const bool skip_request_body_buffering_;
  };
  PerRouteFlags getPerRouteFlags(const Router::RouteConstSharedPtr& route) const;

  // State of this filter's communication with the external authorization service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class State { NotStarted, Calling, Complete };

  // FilterReturn is used to capture what the return code should be to the filter chain.
  // if this filter is either in the middle of calling the service or the result is denied then
  // the filter chain should stop. Otherwise the filter chain can continue to the next filter.
  enum class FilterReturn { ContinueDecoding, StopDecoding };

  Http::HeaderMapPtr getHeaderMap(const Filters::Common::ExtAuthz::ResponsePtr& response);
  FilterConfigSharedPtr config_;
  Filters::Common::ExtAuthz::ClientPtr client_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::RequestHeaderMap* request_headers_;
  Http::HeaderVector response_headers_to_add_{};
  Http::HeaderVector response_headers_to_set_{};
  State state_{State::NotStarted};
  FilterReturn filter_return_{FilterReturn::ContinueDecoding};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  // The stats for the filter.
  ExtAuthzFilterStats stats_;

  // Used to identify if the callback to onComplete() is synchronous (on the stack) or asynchronous.
  bool initiating_call_{};
  bool buffer_data_{};
  bool skip_check_{false};
  envoy::service::auth::v3::CheckRequest check_request_{};
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
