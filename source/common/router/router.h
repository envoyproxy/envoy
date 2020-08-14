#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/access_log/access_log_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/common/cleanup.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/http/utility.h"
#include "common/router/config_impl.h"
#include "common/router/upstream_request.h"
#include "common/stats/symbol_table_impl.h"
#include "common/stream_info/stream_info_impl.h"
#include "common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Router {

/**
 * All router filter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_ROUTER_STATS(COUNTER)                                                                  \
  COUNTER(passthrough_internal_redirect_bad_location)                                              \
  COUNTER(passthrough_internal_redirect_unsafe_scheme)                                             \
  COUNTER(passthrough_internal_redirect_too_many_redirects)                                        \
  COUNTER(passthrough_internal_redirect_no_route)                                                  \
  COUNTER(passthrough_internal_redirect_predicate)                                                 \
  COUNTER(no_route)                                                                                \
  COUNTER(no_cluster)                                                                              \
  COUNTER(rq_redirect)                                                                             \
  COUNTER(rq_direct_response)                                                                      \
  COUNTER(rq_total)                                                                                \
  COUNTER(rq_reset_after_downstream_response_started)
// clang-format on

/**
 * Struct definition for all router filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_ROUTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Router filter utilities split out for ease of testing.
 */
class FilterUtility {
public:
  struct TimeoutData {
    std::chrono::milliseconds global_timeout_{0};
    std::chrono::milliseconds per_try_timeout_{0};
  };

  struct HedgingParams {
    bool hedge_on_per_try_timeout_;
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
   * Set the :scheme header based on whether the underline transport is secure.
   */
  static void setUpstreamScheme(Http::RequestHeaderMap& headers, bool use_secure_transport);

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

  static bool trySetGlobalTimeout(const Http::HeaderEntry* header_timeout_entry,
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
class FilterConfig {
public:
  FilterConfig(const std::string& stat_prefix, const LocalInfo::LocalInfo& local_info,
               Stats::Scope& scope, Upstream::ClusterManager& cm, Runtime::Loader& runtime,
               Random::RandomGenerator& random, ShadowWriterPtr&& shadow_writer,
               bool emit_dynamic_stats, bool start_child_span, bool suppress_envoy_headers,
               bool respect_expected_rq_timeout,
               const Protobuf::RepeatedPtrField<std::string>& strict_check_headers,
               TimeSource& time_source, Http::Context& http_context)
      : scope_(scope), local_info_(local_info), cm_(cm), runtime_(runtime),
        random_(random), stats_{ALL_ROUTER_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix))},
        emit_dynamic_stats_(emit_dynamic_stats), start_child_span_(start_child_span),
        suppress_envoy_headers_(suppress_envoy_headers),
        respect_expected_rq_timeout_(respect_expected_rq_timeout), http_context_(http_context),
        stat_name_pool_(scope_.symbolTable()), retry_(stat_name_pool_.add("retry")),
        zone_name_(stat_name_pool_.add(local_info_.zoneName())),
        empty_stat_name_(stat_name_pool_.add("")), shadow_writer_(std::move(shadow_writer)),
        time_source_(time_source) {
    if (!strict_check_headers.empty()) {
      strict_check_headers_ = std::make_unique<HeaderVector>();
      for (const auto& header : strict_check_headers) {
        strict_check_headers_->emplace_back(Http::LowerCaseString(header));
      }
    }
  }

  FilterConfig(const std::string& stat_prefix, Server::Configuration::FactoryContext& context,
               ShadowWriterPtr&& shadow_writer,
               const envoy::extensions::filters::http::router::v3::Router& config)
      : FilterConfig(stat_prefix, context.localInfo(), context.scope(), context.clusterManager(),
                     context.runtime(), context.random(), std::move(shadow_writer),
                     PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, dynamic_stats, true),
                     config.start_child_span(), config.suppress_envoy_headers(),
                     config.respect_expected_rq_timeout(), config.strict_check_headers(),
                     context.api().timeSource(), context.httpContext()) {
    for (const auto& upstream_log : config.upstream_log()) {
      upstream_logs_.push_back(AccessLog::AccessLogFactory::fromProto(upstream_log, context));
    }
  }
  using HeaderVector = std::vector<Http::LowerCaseString>;
  using HeaderVectorPtr = std::unique_ptr<HeaderVector>;

  ShadowWriter& shadowWriter() { return *shadow_writer_; }
  TimeSource& timeSource() { return time_source_; }

  Stats::Scope& scope_;
  const LocalInfo::LocalInfo& local_info_;
  Upstream::ClusterManager& cm_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  FilterStats stats_;
  const bool emit_dynamic_stats_;
  const bool start_child_span_;
  const bool suppress_envoy_headers_;
  const bool respect_expected_rq_timeout_;
  // TODO(xyu-stripe): Make this a bitset to keep cluster memory footprint down.
  HeaderVectorPtr strict_check_headers_;
  std::list<AccessLog::InstanceSharedPtr> upstream_logs_;
  Http::Context& http_context_;
  Stats::StatNamePool stat_name_pool_;
  Stats::StatName retry_;
  Stats::StatName zone_name_;
  Stats::StatName empty_stat_name_;

private:
  ShadowWriterPtr shadow_writer_;
  TimeSource& time_source_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class UpstreamRequest;
using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

// The interface the UpstreamRequest has to interact with the router filter.
// Split out primarily for unit test mocks.
class RouterFilterInterface {
public:
  virtual ~RouterFilterInterface() = default;

  virtual void onUpstream100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers,
                                            UpstreamRequest& upstream_request) PURE;
  virtual void onUpstreamHeaders(uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
                                 UpstreamRequest& upstream_request, bool end_stream) PURE;
  virtual void onUpstreamData(Buffer::Instance& data, UpstreamRequest& upstream_request,
                              bool end_stream) PURE;
  virtual void onUpstreamTrailers(Http::ResponseTrailerMapPtr&& trailers,
                                  UpstreamRequest& upstream_request) PURE;
  virtual void onUpstreamMetadata(Http::MetadataMapPtr&& metadata_map) PURE;
  virtual void onUpstreamReset(Http::StreamResetReason reset_reason,
                               absl::string_view transport_failure,
                               UpstreamRequest& upstream_request) PURE;
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) PURE;
  virtual void onPerTryTimeout(UpstreamRequest& upstream_request) PURE;
  virtual void onStreamMaxDurationReached(UpstreamRequest& upstream_request) PURE;

  virtual Http::StreamDecoderFilterCallbacks* callbacks() PURE;
  virtual Upstream::ClusterInfoConstSharedPtr cluster() PURE;
  virtual FilterConfig& config() PURE;
  virtual FilterUtility::TimeoutData timeout() PURE;
  virtual Http::RequestHeaderMap* downstreamHeaders() PURE;
  virtual Http::RequestTrailerMap* downstreamTrailers() PURE;
  virtual bool downstreamResponseStarted() const PURE;
  virtual bool downstreamEndStream() const PURE;
  virtual uint32_t attemptCount() const PURE;
  virtual const VirtualCluster* requestVcluster() const PURE;
  virtual const RouteEntry* routeEntry() const PURE;
  virtual const std::list<UpstreamRequestPtr>& upstreamRequests() const PURE;
  virtual const UpstreamRequest* finalUpstreamRequest() const PURE;
  virtual TimeSource& timeSource() PURE;
};

/**
 * Service routing filter.
 */
class Filter : Logger::Loggable<Logger::Id::router>,
               public Http::StreamDecoderFilter,
               public Upstream::LoadBalancerContextBase,
               public RouterFilterInterface {
public:
  Filter(FilterConfig& config)
      : config_(config), final_upstream_request_(nullptr),
        downstream_100_continue_headers_encoded_(false), downstream_response_started_(false),
        downstream_end_stream_(false), is_retry_(false),
        attempting_internal_redirect_with_complete_stream_(false) {}

  ~Filter() override;

  static StreamInfo::ResponseFlag
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
            callbacks_->streamInfo().downstreamRemoteAddress().get(), *downstream_headers_,
            [this](const std::string& key, const std::string& path, std::chrono::seconds max_age) {
              return addDownstreamSetCookie(key, path, max_age);
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
    return callbacks_->connection();
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
    return callbacks_->getUpstreamSocketOptions();
  }

  Network::TransportSocketOptionsSharedPtr upstreamTransportSocketOptions() const override {
    return transport_socket_options_;
  }

  /**
   * Set a computed cookie to be sent with the downstream headers.
   * @param key supplies the size of the cookie
   * @param max_age the lifetime of the cookie
   * @param  path the path of the cookie, or ""
   * @return std::string the value of the new cookie
   */
  std::string addDownstreamSetCookie(const std::string& key, const std::string& path,
                                     std::chrono::seconds max_age) {
    // The cookie value should be the same per connection so that if multiple
    // streams race on the same path, they all receive the same cookie.
    // Since the downstream port is part of the hashed value, multiple HTTP1
    // connections can receive different cookies if they race on requests.
    std::string value;
    const Network::Connection* conn = downstreamConnection();
    // Need to check for null conn if this is ever used by Http::AsyncClient in the future.
    value = conn->remoteAddress()->asString() + conn->localAddress()->asString();

    const std::string cookie_value = Hex::uint64ToHex(HashUtil::xxHash64(value));
    downstream_set_cookies_.emplace_back(
        Http::Utility::makeSetCookieValue(key, cookie_value, path, max_age, true));
    return cookie_value;
  }

  // RouterFilterInterface
  void onUpstream100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers,
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
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) override;
  void onPerTryTimeout(UpstreamRequest& upstream_request) override;
  void onStreamMaxDurationReached(UpstreamRequest& upstream_request) override;
  Http::StreamDecoderFilterCallbacks* callbacks() override { return callbacks_; }
  Upstream::ClusterInfoConstSharedPtr cluster() override { return cluster_; }
  FilterConfig& config() override { return config_; }
  FilterUtility::TimeoutData timeout() override { return timeout_; }
  Http::RequestHeaderMap* downstreamHeaders() override { return downstream_headers_; }
  Http::RequestTrailerMap* downstreamTrailers() override { return downstream_trailers_; }
  bool downstreamResponseStarted() const override { return downstream_response_started_; }
  bool downstreamEndStream() const override { return downstream_end_stream_; }
  uint32_t attemptCount() const override { return attempt_count_; }
  const VirtualCluster* requestVcluster() const override { return request_vcluster_; }
  const RouteEntry* routeEntry() const override { return route_entry_; }
  const std::list<UpstreamRequestPtr>& upstreamRequests() const override {
    return upstream_requests_;
  }
  const UpstreamRequest* finalUpstreamRequest() const override { return final_upstream_request_; }
  TimeSource& timeSource() override { return config_.timeSource(); }

private:
  friend class UpstreamRequest;

  RetryStatePtr retry_state_;

  Stats::StatName upstreamZone(Upstream::HostDescriptionConstSharedPtr upstream_host);
  void chargeUpstreamCode(uint64_t response_status_code,
                          const Http::ResponseHeaderMap& response_headers,
                          Upstream::HostDescriptionConstSharedPtr upstream_host, bool dropped);
  void chargeUpstreamCode(Http::Code code, Upstream::HostDescriptionConstSharedPtr upstream_host,
                          bool dropped);
  void chargeUpstreamAbort(Http::Code code, bool dropped, UpstreamRequest& upstream_request);
  void cleanup();
  virtual RetryStatePtr createRetryState(const RetryPolicy& policy,
                                         Http::RequestHeaderMap& request_headers,
                                         const Upstream::ClusterInfo& cluster,
                                         const VirtualCluster* vcluster, Runtime::Loader& runtime,
                                         Random::RandomGenerator& random,
                                         Event::Dispatcher& dispatcher, TimeSource& time_source,
                                         Upstream::ResourcePriority priority) PURE;

  std::unique_ptr<GenericConnPool> createConnPool();
  UpstreamRequestPtr createUpstreamRequest();

  void maybeDoShadowing();
  bool maybeRetryReset(Http::StreamResetReason reset_reason, UpstreamRequest& upstream_request);
  uint32_t numRequestsAwaitingHeaders();
  void onGlobalTimeout();
  void onRequestComplete();
  void onResponseTimeout();
  // Handle an upstream request aborted due to a local timeout.
  void onSoftPerTryTimeout();
  void onSoftPerTryTimeout(UpstreamRequest& upstream_request);
  void onUpstreamTimeoutAbort(StreamInfo::ResponseFlag response_flag, absl::string_view details);
  // Handle an "aborted" upstream request, meaning we didn't see response
  // headers (e.g. due to a reset). Handles recording stats and responding
  // downstream if appropriate.
  void onUpstreamAbort(Http::Code code, StreamInfo::ResponseFlag response_flag,
                       absl::string_view body, bool dropped, absl::string_view details);
  void onUpstreamComplete(UpstreamRequest& upstream_request);
  // Reset all in-flight upstream requests.
  void resetAll();
  // Reset all in-flight upstream requests that do NOT match the passed argument. This is used
  // if a "good" response comes back and we return downstream, so there is no point in waiting
  // for the remaining upstream requests to return.
  void resetOtherUpstreams(UpstreamRequest& upstream_request);
  void sendNoHealthyUpstreamResponse();
  bool setupRedirect(const Http::ResponseHeaderMap& headers, UpstreamRequest& upstream_request);
  bool convertRequestHeadersForInternalRedirect(Http::RequestHeaderMap& downstream_headers,
                                                const Http::HeaderEntry& internal_redirect);
  void updateOutlierDetection(Upstream::Outlier::Result result, UpstreamRequest& upstream_request,
                              absl::optional<uint64_t> code);
  void doRetry();
  // Called immediately after a non-5xx header is received from upstream, performs stats accounting
  // and handle difference between gRPC and non-gRPC requests.
  void handleNon5xxResponseHeaders(absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                                   UpstreamRequest& upstream_request, bool end_stream,
                                   uint64_t grpc_to_http_status);
  Http::Context& httpContext() { return config_.http_context_; }

  FilterConfig& config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  RouteConstSharedPtr route_;
  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  std::unique_ptr<Stats::StatNameDynamicStorage> alt_stat_prefix_;
  const VirtualCluster* request_vcluster_;
  Event::TimerPtr response_timeout_;
  FilterUtility::TimeoutData timeout_;
  FilterUtility::HedgingParams hedging_params_;
  Http::Code timeout_response_code_ = Http::Code::GatewayTimeout;
  std::list<UpstreamRequestPtr> upstream_requests_;
  // Tracks which upstream request "wins" and will have the corresponding
  // response forwarded downstream
  UpstreamRequest* final_upstream_request_;
  bool grpc_request_{};
  Http::RequestHeaderMap* downstream_headers_{};
  Http::RequestTrailerMap* downstream_trailers_{};
  MonotonicTime downstream_request_complete_time_;
  uint32_t retry_shadow_buffer_limit_{std::numeric_limits<uint32_t>::max()};
  MetadataMatchCriteriaConstPtr metadata_match_;
  std::function<void(Http::ResponseHeaderMap&)> modify_headers_;
  std::vector<std::reference_wrapper<const ShadowPolicy>> active_shadow_policies_{};

  // list of cookies to add to upstream headers
  std::vector<std::string> downstream_set_cookies_;

  bool downstream_100_continue_headers_encoded_ : 1;
  bool downstream_response_started_ : 1;
  bool downstream_end_stream_ : 1;
  bool is_retry_ : 1;
  bool include_attempt_count_in_request_ : 1;
  bool attempting_internal_redirect_with_complete_stream_ : 1;
  uint32_t attempt_count_{1};
  uint32_t pending_retries_{0};

  Network::TransportSocketOptionsSharedPtr transport_socket_options_;
};

class ProdFilter : public Filter {
public:
  using Filter::Filter;

private:
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy& policy, Http::RequestHeaderMap& request_headers,
                                 const Upstream::ClusterInfo& cluster,
                                 const VirtualCluster* vcluster, Runtime::Loader& runtime,
                                 Random::RandomGenerator& random, Event::Dispatcher& dispatcher,
                                 TimeSource& time_source,
                                 Upstream::ResourcePriority priority) override;
};

} // namespace Router
} // namespace Envoy
