#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/matchers.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/common/ext_authz/ext_authz.h"
#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { Internal, External, Both };

/**
 * All stats for the Ext Authz filter. @see stats_macros.h
 */

#define ALL_EXT_AUTHZ_FILTER_STATS(COUNTER)                                                        \
  COUNTER(ok)                                                                                      \
  COUNTER(denied)                                                                                  \
  COUNTER(error)                                                                                   \
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
public:
  FilterConfig(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& config,
               const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
               Runtime::Loader& runtime, Http::Context& http_context,
               const std::string& stats_prefix)
      : allow_partial_message_(config.with_request_body().allow_partial_message()),
        failure_mode_allow_(config.failure_mode_allow()),
        clear_route_cache_(config.clear_route_cache()),
        max_request_bytes_(config.with_request_body().max_request_bytes()),
        status_on_error_(toErrorCode(config.status_on_error().code())), local_info_(local_info),
        scope_(scope), runtime_(runtime), http_context_(http_context),
        filter_enabled_(config.has_filter_enabled()
                            ? absl::optional<Runtime::FractionalPercent>(
                                  Runtime::FractionalPercent(config.filter_enabled(), runtime_))
                            : absl::nullopt),
        pool_(scope_.symbolTable()),
        metadata_context_namespaces_(config.metadata_context_namespaces().begin(),
                                     config.metadata_context_namespaces().end()),
        include_peer_certificate_(config.include_peer_certificate()),
        stats_(generateStats(stats_prefix, scope)), ext_authz_ok_(pool_.add("ext_authz.ok")),
        ext_authz_denied_(pool_.add("ext_authz.denied")),
        ext_authz_error_(pool_.add("ext_authz.error")),
        ext_authz_failure_mode_allowed_(pool_.add("ext_authz.failure_mode_allowed")) {}

  bool allowPartialMessage() const { return allow_partial_message_; }

  bool withRequestBody() const { return max_request_bytes_ > 0; }

  bool failureModeAllow() const { return failure_mode_allow_; }

  bool clearRouteCache() const { return clear_route_cache_; }

  uint32_t maxRequestBytes() const { return max_request_bytes_; }

  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }

  Http::Code statusOnError() const { return status_on_error_; }

  bool filterEnabled() { return filter_enabled_.has_value() ? filter_enabled_->enabled() : true; }

  Runtime::Loader& runtime() { return runtime_; }

  Stats::Scope& scope() { return scope_; }

  Http::Context& httpContext() { return http_context_; }

  const std::vector<std::string>& metadataContextNamespaces() {
    return metadata_context_namespaces_;
  }

  const ExtAuthzFilterStats& stats() const { return stats_; }

  void incCounter(Stats::Scope& scope, Stats::StatName name) {
    scope.counterFromStatName(name).inc();
  }

  bool includePeerCertificate() const { return include_peer_certificate_; }

private:
  static Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Http::Code>(status);
    if (code >= Http::Code::Continue && code <= Http::Code::NetworkAuthenticationRequired) {
      return code;
    }
    return Http::Code::Forbidden;
  }

  ExtAuthzFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    const std::string final_prefix = prefix + "ext_authz.";
    return {ALL_EXT_AUTHZ_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  const bool allow_partial_message_;
  const bool failure_mode_allow_;
  const bool clear_route_cache_;
  const uint32_t max_request_bytes_;
  const Http::Code status_on_error_;
  const LocalInfo::LocalInfo& local_info_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Http::Context& http_context_;

  const absl::optional<Runtime::FractionalPercent> filter_enabled_;

  // TODO(nezdolik): stop using pool as part of deprecating cluster scope stats.
  Stats::StatNamePool pool_;

  const std::vector<std::string> metadata_context_namespaces_;

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
        disabled_(config.disabled()) {}

  void merge(const FilterConfigPerRoute& other);

  /**
   * @return Context extensions to add to the CheckRequest.
   */
  const ContextExtensionsMap& contextExtensions() const { return context_extensions_; }
  // Allow moving the context extensions out of this object.
  ContextExtensionsMap&& takeContextExtensions() { return std::move(context_extensions_); }

  bool disabled() const { return disabled_; }

private:
  // We save the context extensions as a protobuf map instead of an std::map as this allows us to
  // move it to the CheckRequest, thus avoiding a copy that would incur by converting it.
  ContextExtensionsMap context_extensions_;
  bool disabled_;
};

/**
 * HTTP ext_authz filter. Depending on the route configuration, this filter calls the global
 * ext_authz service before allowing further filter iteration.
 */
class Filter : public Logger::Loggable<Logger::Id::filter>,
               public Http::StreamDecoderFilter,
               public Filters::Common::ExtAuthz::RequestCallbacks {
public:
  Filter(FilterConfigSharedPtr config, Filters::Common::ExtAuthz::ClientPtr&& client)
      : config_(config), client_(std::move(client)), stats_(config->stats()) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // ExtAuthz::RequestCallbacks
  void onComplete(Filters::Common::ExtAuthz::ResponsePtr&&) override;

private:
  void addResponseHeaders(Http::HeaderMap& header_map, const Http::HeaderVector& headers);
  void initiateCall(const Http::HeaderMap& headers);
  void continueDecoding();
  bool isBufferFull();

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
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::HeaderMap* request_headers_;
  State state_{State::NotStarted};
  FilterReturn filter_return_{FilterReturn::ContinueDecoding};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  // The stats for the filter.
  ExtAuthzFilterStats stats_;

  // Used to identify if the callback to onComplete() is synchronous (on the stack) or asynchronous.
  bool initiating_call_{};
  bool buffer_data_{};
  envoy::service::auth::v3::CheckRequest check_request_{};
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
