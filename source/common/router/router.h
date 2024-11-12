#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/filter_factory.h"
#include "envoy/http/hash_policy.h"
#include "envoy/http/stateful_session.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/router_filter_interface.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/hash.h"
#include "source/common/common/hex.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/filter_chain_helper.h"
#include "source/common/http/sidestream_watermark.h"
#include "source/common/http/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/context_impl.h"
#include "source/common/router/upstream_request.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_factory_context_impl.h"

namespace Envoy {
namespace Router {

/**
 * Struct definition for all router filter stats. @see stats_macros.h
 */
MAKE_STATS_STRUCT(FilterStats, StatNames, ALL_ROUTER_STATS);

/**
 * Router filter utilities split out for ease of testing.
 */
class FilterUtility {
public:
  struct HedgingParams {
    bool hedge_on_per_try_timeout_ : 1;
  };

  class StrictHeaderChecker {
  public:
    struct HeaderCheckResult {
      bool valid_ = true;
      const Http::HeaderEntry* entry_;
    };

    /**
     * Determine whether a given header's value passes the strict validation
     * defined for that header.
     * @param headers supplies the headers from which to get the target header.
     * @param target_header is the header to be validated.
     * @return HeaderCheckResult containing the entry for @param target_header
     *         and valid_ set to FALSE if @param target_header is set to an
     *         invalid value. If @param target_header doesn't appear in
     *         @param headers, return a result with valid_ set to TRUE.
     */
    static const HeaderCheckResult checkHeader(Http::RequestHeaderMap& headers,
                                               const Http::LowerCaseString& target_header);

    using ParseRetryFlagsFunc = std::function<std::pair<uint32_t, bool>(absl::string_view)>;

  private:
    static HeaderCheckResult hasValidRetryFields(const Http::HeaderEntry* header_entry,
                                                 const ParseRetryFlagsFunc& parse_fn) {
      HeaderCheckResult r;
      if (header_entry) {
        const auto flags_and_validity = parse_fn(header_entry->value().getStringView());
        r.valid_ = flags_and_validity.second;
        r.entry_ = header_entry;
      }
      return r;
    }

    static HeaderCheckResult isInteger(const Http::HeaderEntry* header_entry) {
      HeaderCheckResult r;
      if (header_entry) {
        uint64_t out;
        r.valid_ = absl::SimpleAtoi(header_entry->value().getStringView(), &out);
        r.entry_ = header_entry;
      }
      return r;
    }
  };

  /**
   * Returns response_time / timeout, as a  percentage as [0, 100]. Returns 0
   * if there is no timeout.
   * @param response_time supplies the response time thus far.
   * @param timeout supplies  the timeout to get the percentage of.
   * @return the percentage of timeout [0, 100] for stats use.
   */
  static uint64_t percentageOfTimeout(const std::chrono::milliseconds response_time,
                                      const std::chrono::milliseconds timeout);

  /**
   * Set the :scheme header using the best information available. In order this is
   * - whether the upstream connection is using TLS if use_upstream is true
   * - existing scheme header if valid
   * - x-forwarded-proto header if valid
   * - whether the downstream connection is using TLS
   */
  static void setUpstreamScheme(Http::RequestHeaderMap& headers, bool downstream_ssl,
                                bool upstream_ssl, bool use_upstream);

  /**
   * Determine whether a request should be shadowed.
   * @param policy supplies the route's shadow policy.
   * @param runtime supplies the runtime to lookup the shadow key in.
   * @param stable_random supplies the random number to use when determining whether shadowing
   *        should take place.
   * @return TRUE if shadowing should take place.
   */
  static bool shouldShadow(const ShadowPolicy& policy, Runtime::Loader& runtime,
                           uint64_t stable_random);

  /**
   * Determine the final timeout to use based on the route as well as the request headers.
   * @param route supplies the request route.
   * @param request_headers supplies the request headers.
   * @param insert_envoy_expected_request_timeout_ms insert
   *        x-envoy-expected-request-timeout-ms?
   * @param grpc_request tells if the request is a gRPC request.
   * @return TimeoutData for both the global and per try timeouts.
   */
  static TimeoutData finalTimeout(const RouteEntry& route, Http::RequestHeaderMap& request_headers,
                                  bool insert_envoy_expected_request_timeout_ms, bool grpc_request,
                                  bool per_try_timeout_hedging_enabled,
                                  bool respect_expected_rq_timeout);

  /**
   * Set the x-envoy-expected-request-timeout-ms and grpc-timeout headers if needed.
   * @param elapsed_time time elapsed since completion of the downstream request
   * @param timeout final TimeoutData to use for the request
   * @param request_headers the request headers to modify
   * @param insert_envoy_expected_request_timeout_ms insert
   *        x-envoy-expected-request-timeout-ms?
   * @param grpc_request tells if the request is a gRPC request.
   * @param per_try_timeout_headging_enabled is request hedging enabled?
   */
  static void setTimeoutHeaders(uint64_t elapsed_time, const TimeoutData& timeout,
                                const RouteEntry& route, Http::RequestHeaderMap& request_headers,
                                bool insert_envoy_expected_request_timeout_ms, bool grpc_request,
                                bool per_try_timeout_hedging_enabled);

  /**
   * Try to parse a header entry that may have a timeout field
   *
   * @param header_timeout_entry header entry which may contain a timeout value.
   * @return result timeout value from header. It will return nullopt if parse failed.
   */
  static absl::optional<std::chrono::milliseconds>
  tryParseHeaderTimeout(const Http::HeaderEntry& header_timeout_entry);

  /**
   * Try to set global timeout.
   *
   * @param header_timeout_entry header entry which may contain a timeout value.
   * @param timeout timeout data to set from header timeout entry.
   */
  static void trySetGlobalTimeout(const Http::HeaderEntry& header_timeout_entry,
                                  TimeoutData& timeout);

  /**
   * Determine the final hedging settings after applying randomized behavior.
   * @param route supplies the request route.
   * @param request_headers supplies the request headers.
   * @return HedgingParams the final parameters to use for request hedging.
   */
  static HedgingParams finalHedgingParams(const RouteEntry& route,
                                          Http::RequestHeaderMap& request_headers);
};

/**
 * Configuration for the router filter.
 */
class FilterConfig : Http::FilterChainFactory {
public:
  FilterConfig(Server::Configuration::CommonFactoryContext& factory_context,
               Stats::StatName stat_prefix, const LocalInfo::LocalInfo& local_info,
               Stats::Scope& scope, Upstream::ClusterManager& cm, Runtime::Loader& runtime,
               Random::RandomGenerator& random, ShadowWriterPtr&& shadow_writer,
               bool emit_dynamic_stats, bool start_child_span, bool suppress_envoy_headers,
               bool respect_expected_rq_timeout, bool suppress_grpc_request_failure_code_stats,
               bool flush_upstream_log_on_upstream_stream,
               const Protobuf::RepeatedPtrField<std::string>& strict_check_headers,
               TimeSource& time_source, Http::Context& http_context,
               Router::Context& router_context)
      : factory_context_(factory_context), router_context_(router_context), scope_(scope),
        local_info_(local_info), cm_(cm), runtime_(runtime),
        default_stats_(router_context_.statNames(), scope_, stat_prefix),
        async_stats_(router_context_.statNames(), scope, http_context.asyncClientStatPrefix()),
        random_(random), emit_dynamic_stats_(emit_dynamic_stats),
        start_child_span_(start_child_span), suppress_envoy_headers_(suppress_envoy_headers),
        respect_expected_rq_timeout_(respect_expected_rq_timeout),
        suppress_grpc_request_failure_code_stats_(suppress_grpc_request_failure_code_stats),
        flush_upstream_log_on_upstream_stream_(flush_upstream_log_on_upstream_stream),
        http_context_(http_context), zone_name_(local_info_.zoneStatName()),
        shadow_writer_(std::move(shadow_writer)), time_source_(time_source) {
    if (!strict_check_headers.empty()) {
      strict_check_headers_ = std::make_unique<HeaderVector>();
      for (const auto& header : strict_check_headers) {
        strict_check_headers_->emplace_back(Http::LowerCaseString(header));
      }
    }
  }

  FilterConfig(Stats::StatName stat_prefix, Server::Configuration::FactoryContext& context,
               ShadowWriterPtr&& shadow_writer,
               const envoy::extensions::filters::http::router::v3::Router& config);

  bool createFilterChain(
      Http::FilterChainManager& manager, bool only_create_if_configured = false,
      const Http::FilterChainOptions& options = Http::EmptyFilterChainOptions{}) const override {
    // Currently there is no default filter chain, so only_create_if_configured true doesn't make
    // sense.
    ASSERT(!only_create_if_configured);
    if (upstream_http_filter_factories_.empty()) {
      return false;
    }
    Http::FilterChainUtility::createFilterChainForFactories(manager, options,
                                                            upstream_http_filter_factories_);
    return true;
  }

  bool createUpgradeFilterChain(absl::string_view, const UpgradeMap*, Http::FilterChainManager&,
                                const Http::FilterChainOptions&) const override {
    // Upgrade filter chains not yet supported for upstream HTTP filters.
    return false;
  }

  using HeaderVector = std::vector<Http::LowerCaseString>;
  using HeaderVectorPtr = std::unique_ptr<HeaderVector>;

  ShadowWriter& shadowWriter() { return *shadow_writer_; }
  TimeSource& timeSource() { return time_source_; }

  Server::Configuration::CommonFactoryContext& factory_context_;
  Router::Context& router_context_;
  Stats::Scope& scope_;
  const LocalInfo::LocalInfo& local_info_;
  Upstream::ClusterManager& cm_;
  Runtime::Loader& runtime_;
  FilterStats default_stats_;
  FilterStats async_stats_;
  Random::RandomGenerator& random_;
  const bool emit_dynamic_stats_;
  const bool start_child_span_;
  const bool suppress_envoy_headers_;
  const bool respect_expected_rq_timeout_;
  const bool suppress_grpc_request_failure_code_stats_;
  // TODO(xyu-stripe): Make this a bitset to keep cluster memory footprint down.
  HeaderVectorPtr strict_check_headers_;
  const bool flush_upstream_log_on_upstream_stream_;
  absl::optional<std::chrono::milliseconds> upstream_log_flush_interval_;
  std::list<AccessLog::InstanceSharedPtr> upstream_logs_;
  Http::Context& http_context_;
  Stats::StatName zone_name_;
  Stats::StatName empty_stat_name_;
  std::unique_ptr<Server::Configuration::UpstreamFactoryContext> upstream_ctx_;
  Http::FilterChainUtility::FilterFactoriesList upstream_http_filter_factories_;

private:
  ShadowWriterPtr shadow_writer_;
  TimeSource& time_source_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class UpstreamRequest;
using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

/**
 * Service routing filter.
 */
class Filter : Logger::Loggable<Logger::Id::router>,
               public Http::StreamDecoderFilter,
               public Upstream::LoadBalancerContextBase,
               public RouterFilterInterface {
public:
  Filter(const FilterConfigSharedPtr& config, FilterStats& stats)
      : config_(config), stats_(stats), grpc_request_(false), exclude_http_code_stats_(false),
        downstream_response_started_(false), downstream_end_stream_(false), is_retry_(false),
        request_buffer_overflowed_(false), streaming_shadows_(Runtime::runtimeFeatureEnabled(
                                               "envoy.reloadable_features.streaming_shadow")),
        allow_multiplexed_upstream_half_close_(Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.allow_multiplexed_upstream_half_close")),
        upstream_request_started_(false), orca_load_report_received_(false) {}

  ~Filter() override;

  static StreamInfo::CoreResponseFlag
  streamResetReasonToResponseFlag(Http::StreamResetReason reset_reason);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override {
    if (route_entry_ && downstream_headers_) {
      auto hash_policy = route_entry_->hashPolicy();
      if (hash_policy) {
        return hash_policy->generateHash(
            callbacks_->streamInfo().downstreamAddressProvider().remoteAddress().get(),
            *downstream_headers_,
            [this](const std::string& key, const std::string& path, std::chrono::seconds max_age,
                   Http::CookieAttributeRefVector attributes) {
              return addDownstreamSetCookie(key, path, max_age, attributes);
            },
            callbacks_->streamInfo().filterState());
      }
    }
    return {};
  }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    if (route_entry_) {
      // Have we been called before? If so, there's no need to recompute because
      // by the time this method is called for the first time, route_entry_ should
      // not change anymore.
      if (metadata_match_ != nullptr) {
        return metadata_match_.get();
      }

      // The request's metadata, if present, takes precedence over the route's.
      const auto& request_metadata = callbacks_->streamInfo().dynamicMetadata().filter_metadata();
      const auto filter_it = request_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);
      if (filter_it != request_metadata.end()) {
        if (route_entry_->metadataMatchCriteria() != nullptr) {
          metadata_match_ =
              route_entry_->metadataMatchCriteria()->mergeMatchCriteria(filter_it->second);
        } else {
          metadata_match_ = std::make_unique<Router::MetadataMatchCriteriaImpl>(filter_it->second);
        }
        return metadata_match_.get();
      }
      return route_entry_->metadataMatchCriteria();
    }
    return nullptr;
  }
  const Network::Connection* downstreamConnection() const override {
    return callbacks_->connection().ptr();
  }
  const StreamInfo::StreamInfo* requestStreamInfo() const override {
    return &callbacks_->streamInfo();
  }
  const Http::RequestHeaderMap* downstreamHeaders() const override { return downstream_headers_; }

  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    // We only care about host selection when performing a retry, at which point we consult the
    // RetryState to see if we're configured to avoid certain hosts during retries.
    if (!is_retry_) {
      return false;
    }

    ASSERT(retry_state_);
    return retry_state_->shouldSelectAnotherHost(host);
  }

  const Upstream::HealthyAndDegradedLoad& determinePriorityLoad(
      const Upstream::PrioritySet& priority_set,
      const Upstream::HealthyAndDegradedLoad& original_priority_load,
      const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) override {
    // We only modify the priority load on retries.
    if (!is_retry_) {
      return original_priority_load;
    }
    return retry_state_->priorityLoadForRetry(priority_set, original_priority_load,
                                              priority_mapping_func);
  }

  uint32_t hostSelectionRetryCount() const override {
    if (!is_retry_) {
      return 1;
    }
    return retry_state_->hostSelectionMaxAttempts();
  }

  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return (upstream_options_ != nullptr) ? upstream_options_
                                          : callbacks_->getUpstreamSocketOptions();
  }

  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return transport_socket_options_;
  }

  absl::optional<OverrideHost> overrideHostToSelect() const override {
    if (is_retry_) {
      return {};
    }
    return callbacks_->upstreamOverrideHost();
  }

  void setOrcaLoadReportCallbacks(std::weak_ptr<OrcaLoadReportCallbacks> callbacks) override {
    orca_load_report_callbacks_ = callbacks;
  }

  /**
   * Set a computed cookie to be sent with the downstream headers.
   * @param key supplies the size of the cookie
   * @param max_age the lifetime of the cookie
   * @param  path the path of the cookie, or ""
   * @return std::string the value of the new cookie
   */
  std::string addDownstreamSetCookie(const std::string& key, const std::string& path,
                                     std::chrono::seconds max_age,
                                     Http::CookieAttributeRefVector attributes) {
    // The cookie value should be the same per connection so that if multiple
    // streams race on the same path, they all receive the same cookie.
    // Since the downstream port is part of the hashed value, multiple HTTP1
    // connections can receive different cookies if they race on requests.
    std::string value;
    const Network::Connection* conn = downstreamConnection();
    // Need to check for null conn if this is ever used by Http::AsyncClient in the future.
    value = conn->connectionInfoProvider().remoteAddress()->asString() +
            conn->connectionInfoProvider().localAddress()->asString();

    const std::string cookie_value = Hex::uint64ToHex(HashUtil::xxHash64(value));
    downstream_set_cookies_.emplace_back(
        Http::Utility::makeSetCookieValue(key, cookie_value, path, max_age, true, attributes));
    return cookie_value;
  }

  // RouterFilterInterface
  void onUpstream1xxHeaders(Http::ResponseHeaderMapPtr&& headers,
                            UpstreamRequest& upstream_request) override;
  void onUpstreamHeaders(uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
                         UpstreamRequest& upstream_request, bool end_stream) override;
  void onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                      bool end_stream) override;
  void onUpstreamTrailers(Http::ResponseTrailerMapPtr&& trailers,
                          UpstreamRequest& upstream_request) override;
  void onUpstreamMetadata(Http::MetadataMapPtr&& metadata_map) override;
  void onUpstreamReset(Http::StreamResetReason reset_reason, absl::string_view transport_failure,
                       UpstreamRequest& upstream_request) override;
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host,
                              bool pool_success) override;
  void onPerTryTimeout(UpstreamRequest& upstream_request) override;
  void onPerTryIdleTimeout(UpstreamRequest& upstream_request) override;
  void onStreamMaxDurationReached(UpstreamRequest& upstream_request) override;
  Http::StreamDecoderFilterCallbacks* callbacks() override { return callbacks_; }
  Upstream::ClusterInfoConstSharedPtr cluster() override { return cluster_; }
  FilterConfig& config() override { return *config_; }
  TimeoutData timeout() override { return timeout_; }
  absl::optional<std::chrono::milliseconds> dynamicMaxStreamDuration() const override {
    return dynamic_max_stream_duration_;
  }
  Http::RequestHeaderMap* downstreamHeaders() override { return downstream_headers_; }
  Http::RequestTrailerMap* downstreamTrailers() override { return downstream_trailers_; }
  bool downstreamResponseStarted() const override { return downstream_response_started_; }
  bool downstreamEndStream() const override { return downstream_end_stream_; }
  uint32_t attemptCount() const override { return attempt_count_; }
  const std::list<UpstreamRequestPtr>& upstreamRequests() const { return upstream_requests_; }

  TimeSource& timeSource() { return config_->timeSource(); }
  const Route* route() const { return route_.get(); }
  const FilterStats& stats() { return stats_; }

protected:
  void setRetryShadowBufferLimit(uint32_t retry_shadow_buffer_limit) {
    ASSERT(retry_shadow_buffer_limit_ > retry_shadow_buffer_limit);
    retry_shadow_buffer_limit_ = retry_shadow_buffer_limit;
  }

private:
  friend class UpstreamRequest;

  enum class TimeoutRetry { Yes, No };

  void onPerTryTimeoutCommon(UpstreamRequest& upstream_request, Stats::Counter& error_counter,
                             const std::string& response_code_details);
  Stats::StatName upstreamZone(Upstream::HostDescriptionConstSharedPtr upstream_host);
  void chargeUpstreamCode(uint64_t response_status_code,
                          const Http::ResponseHeaderMap& response_headers,
                          Upstream::HostDescriptionConstSharedPtr upstream_host, bool dropped);
  void chargeUpstreamCode(Http::Code code, Upstream::HostDescriptionConstSharedPtr upstream_host,
                          bool dropped);
  void chargeUpstreamAbort(Http::Code code, bool dropped, UpstreamRequest& upstream_request);
  void cleanup();
  virtual RetryStatePtr
  createRetryState(const RetryPolicy& policy, Http::RequestHeaderMap& request_headers,
                   const Upstream::ClusterInfo& cluster, const VirtualCluster* vcluster,
                   RouteStatsContextOptRef route_stats_context,
                   Server::Configuration::CommonFactoryContext& context,
                   Event::Dispatcher& dispatcher, Upstream::ResourcePriority priority) PURE;

  std::unique_ptr<GenericConnPool>
  createConnPool(Upstream::ThreadLocalCluster& thread_local_cluster);
  UpstreamRequestPtr createUpstreamRequest();
  absl::optional<absl::string_view> getShadowCluster(const ShadowPolicy& shadow_policy,
                                                     const Http::HeaderMap& headers) const;

  void maybeDoShadowing();
  bool maybeRetryReset(Http::StreamResetReason reset_reason, UpstreamRequest& upstream_request,
                       TimeoutRetry is_timeout_retry);
  uint32_t numRequestsAwaitingHeaders();
  void onGlobalTimeout();
  void onRequestComplete();
  void onResponseTimeout();
  // Handle an upstream request aborted due to a local timeout.
  void onSoftPerTryTimeout();
  void onSoftPerTryTimeout(UpstreamRequest& upstream_request);
  void onUpstreamTimeoutAbort(StreamInfo::CoreResponseFlag response_flag,
                              absl::string_view details);
  // Handle an "aborted" upstream request, meaning we didn't see response
  // headers (e.g. due to a reset). Handles recording stats and responding
  // downstream if appropriate.
  void onUpstreamAbort(Http::Code code, StreamInfo::CoreResponseFlag response_flag,
                       absl::string_view body, bool dropped, absl::string_view details);
  void onUpstreamComplete(UpstreamRequest& upstream_request);
  // Reset all in-flight upstream requests.
  void resetAll();
  // Reset all in-flight upstream requests that do NOT match the passed argument. This is used
  // if a "good" response comes back and we return downstream, so there is no point in waiting
  // for the remaining upstream requests to return.
  void resetOtherUpstreams(UpstreamRequest& upstream_request);
  void sendNoHealthyUpstreamResponse();
  bool setupRedirect(const Http::ResponseHeaderMap& headers);
  bool convertRequestHeadersForInternalRedirect(Http::RequestHeaderMap& downstream_headers,
                                                const Http::ResponseHeaderMap& upstream_headers,
                                                const Http::HeaderEntry& internal_redirect,
                                                uint64_t status_code);
  void updateOutlierDetection(Upstream::Outlier::Result result, UpstreamRequest& upstream_request,
                              absl::optional<uint64_t> code);
  void doRetry(bool can_send_early_data, bool can_use_http3, TimeoutRetry is_timeout_retry);
  void runRetryOptionsPredicates(UpstreamRequest& retriable_request);
  // Called immediately after a non-5xx header is received from upstream, performs stats accounting
  // and handle difference between gRPC and non-gRPC requests.
  void handleNon5xxResponseHeaders(absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                                   UpstreamRequest& upstream_request, bool end_stream,
                                   uint64_t grpc_to_http_status);
  Http::Context& httpContext() { return config_->http_context_; }
  bool checkDropOverload(Upstream::ThreadLocalCluster& cluster,
                         std::function<void(Http::ResponseHeaderMap&)>& modify_headers);
  // Process Orca Load Report if necessary (e.g. cluster has lrsReportMetricNames).
  void maybeProcessOrcaLoadReport(const Envoy::Http::HeaderMap& headers_or_trailers,
                                  UpstreamRequest& upstream_request);

  RetryStatePtr retry_state_;
  const FilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  RouteConstSharedPtr route_;
  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  std::unique_ptr<Stats::StatNameDynamicStorage> alt_stat_prefix_;
  const VirtualCluster* request_vcluster_{};
  RouteStatsContextOptRef route_stats_context_;
  Event::TimerPtr response_timeout_;
  TimeoutData timeout_;
  std::list<UpstreamRequestPtr> upstream_requests_;
  FilterStats stats_;
  // Tracks which upstream request "wins" and will have the corresponding
  // response forwarded downstream
  UpstreamRequest* final_upstream_request_ = nullptr;
  Http::RequestHeaderMap* downstream_headers_{};
  Http::RequestTrailerMap* downstream_trailers_{};
  MonotonicTime downstream_request_complete_time_;
  MetadataMatchCriteriaConstPtr metadata_match_;
  std::function<void(Http::ResponseHeaderMap&)> modify_headers_;
  std::vector<std::reference_wrapper<const ShadowPolicy>> active_shadow_policies_{};
  std::unique_ptr<Http::RequestHeaderMap> shadow_headers_;
  std::unique_ptr<Http::RequestTrailerMap> shadow_trailers_;
  // The stream lifetime configured by request header.
  absl::optional<std::chrono::milliseconds> dynamic_max_stream_duration_;
  // list of cookies to add to upstream headers
  std::vector<std::string> downstream_set_cookies_;

  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  Network::Socket::OptionsSharedPtr upstream_options_;
  // Set of ongoing shadow streams which have not yet received end stream.
  absl::flat_hash_set<Http::AsyncClient::OngoingRequest*> shadow_streams_;

  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  uint32_t retry_shadow_buffer_limit_{std::numeric_limits<uint32_t>::max()};
  uint32_t attempt_count_{1};
  uint32_t pending_retries_{0};
  Http::Code timeout_response_code_ = Http::Code::GatewayTimeout;
  FilterUtility::HedgingParams hedging_params_;
  Http::StreamFilterSidestreamWatermarkCallbacks watermark_callbacks_;
  std::weak_ptr<OrcaLoadReportCallbacks> orca_load_report_callbacks_;
  bool grpc_request_ : 1;
  bool exclude_http_code_stats_ : 1;
  bool downstream_response_started_ : 1;
  bool downstream_end_stream_ : 1;
  bool is_retry_ : 1;
  bool include_attempt_count_in_request_ : 1;
  bool include_timeout_retry_header_in_request_ : 1;
  bool request_buffer_overflowed_ : 1;
  const bool streaming_shadows_ : 1;
  const bool allow_multiplexed_upstream_half_close_ : 1;
  bool upstream_request_started_ : 1;
  // Indicate that ORCA report is received to process it only once in either response headers or
  // trailers.
  bool orca_load_report_received_ : 1;
};

class ProdFilter : public Filter {
public:
  using Filter::Filter;

private:
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy& policy, Http::RequestHeaderMap& request_headers,
                                 const Upstream::ClusterInfo& cluster,
                                 const VirtualCluster* vcluster,
                                 RouteStatsContextOptRef route_stats_context,
                                 Server::Configuration::CommonFactoryContext& context,
                                 Event::Dispatcher& dispatcher,
                                 Upstream::ResourcePriority priority) override;
};

} // namespace Router
} // namespace Envoy
