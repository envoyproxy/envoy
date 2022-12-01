#pragma once

#include <chrono>
#include <cstdint>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"
#include "envoy/router/cluster_specifier_plugin.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/matchers.h"
#include "source/common/config/metadata.h"
#include "source/common/http/hash_policy.h"
#include "source/common/http/header_utility.h"
#include "source/common/matcher/matcher.h"
#include "source/common/router/config_utility.h"
#include "source/common/router/header_formatter.h"
#include "source/common/router/header_parser.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/router_ratelimit.h"
#include "source/common/router/tls_context_match_criteria_impl.h"
#include "source/common/stats/symbol_table.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

/**
 * Original port from the authority header.
 */
class OriginalConnectPort : public StreamInfo::FilterState::Object {
public:
  explicit OriginalConnectPort(uint32_t port) : port_(port) {}
  const uint32_t& value() const { return port_; }
  static const std::string& key();

private:
  const uint32_t port_;
};

/**
 * Base interface for something that matches a header.
 */
class Matchable {
public:
  virtual ~Matchable() = default;

  /**
   * See if this object matches the incoming headers.
   * @param headers supplies the headers to match.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return true if input headers match this object.
   */
  virtual RouteConstSharedPtr matches(const Http::RequestHeaderMap& headers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      uint64_t random_value) const PURE;

  // By default, matchers do not support null Path headers.
  virtual bool supportsPathlessHeaders() const { return false; }
};

using OptionalHttpFilters = absl::flat_hash_set<std::string>;

class PerFilterConfigs : public Logger::Loggable<Logger::Id::http> {
public:
  PerFilterConfigs(const Protobuf::Map<std::string, ProtobufWkt::Any>& typed_configs,
                   const OptionalHttpFilters& optional_http_filters,
                   Server::Configuration::ServerFactoryContext& factory_context,
                   ProtobufMessage::ValidationVisitor& validator);

  const RouteSpecificFilterConfig* get(const std::string& name) const;

private:
  RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const std::string& name, const ProtobufWkt::Any& typed_config,
                                  const OptionalHttpFilters& optional_http_filters,
                                  Server::Configuration::ServerFactoryContext& factory_context,
                                  ProtobufMessage::ValidationVisitor& validator);

  absl::node_hash_map<std::string, RouteSpecificFilterConfigConstSharedPtr> configs_;
};

class RouteEntryImplBase;
using RouteEntryImplBaseConstSharedPtr = std::shared_ptr<const RouteEntryImplBase>;

/**
 * Direct response entry that does an SSL redirect.
 */
class SslRedirector : public DirectResponseEntry {
public:
  // Router::DirectResponseEntry
  void finalizeResponseHeaders(Http::ResponseHeaderMap&,
                               const StreamInfo::StreamInfo&) const override {}
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo&,
                                                  bool) const override {
    return {};
  }
  std::string newPath(const Http::RequestHeaderMap& headers) const override;
  void rewritePathHeader(Http::RequestHeaderMap&, bool) const override {}
  Http::Code responseCode() const override { return Http::Code::MovedPermanently; }
  const std::string& responseBody() const override { return EMPTY_STRING; }
  const std::string& routeName() const override { return route_name_; }

private:
  const std::string route_name_;
};

class SslRedirectRoute : public Route {
public:
  // Router::Route
  const DirectResponseEntry* directResponseEntry() const override { return &SSL_REDIRECTOR; }
  const RouteEntry* routeEntry() const override { return nullptr; }
  const Decorator* decorator() const override { return nullptr; }
  const RouteTracing* tracingConfig() const override { return nullptr; }
  const RouteSpecificFilterConfig* mostSpecificPerFilterConfig(const std::string&) const override {
    return nullptr;
  }
  void traversePerFilterConfig(
      const std::string&,
      std::function<void(const Router::RouteSpecificFilterConfig&)>) const override {}
  const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }
  const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }

private:
  static const SslRedirector SSL_REDIRECTOR;
  static const envoy::config::core::v3::Metadata metadata_;
  static const Envoy::Config::TypedMetadataImpl<Envoy::Config::TypedMetadataFactory>
      typed_metadata_;
};

/**
 * Implementation of CorsPolicy that reads from the proto route and virtual host config.
 * TODO(wbpcode): move all cors interfaces and implementation to 'extensions/filters/http/cors'.
 */
template <class ProtoType> class CorsPolicyImplBase : public CorsPolicy {
public:
  CorsPolicyImplBase(const ProtoType& config, Runtime::Loader& loader)
      : config_(config), loader_(loader), allow_methods_(config.allow_methods()),
        allow_headers_(config.allow_headers()), expose_headers_(config.expose_headers()),
        max_age_(config.max_age()) {
    for (const auto& string_match : config.allow_origin_string_match()) {
      allow_origins_.push_back(
          std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
              string_match));
    }
    if (config.has_allow_credentials()) {
      allow_credentials_ = PROTOBUF_GET_WRAPPED_REQUIRED(config, allow_credentials);
    }
    if (config.has_allow_private_network_access()) {
      allow_private_network_access_ =
          PROTOBUF_GET_WRAPPED_REQUIRED(config, allow_private_network_access);
    }
  }

  // Router::CorsPolicy
  const std::vector<Matchers::StringMatcherPtr>& allowOrigins() const override {
    return allow_origins_;
  };
  const std::string& allowMethods() const override { return allow_methods_; };
  const std::string& allowHeaders() const override { return allow_headers_; };
  const std::string& exposeHeaders() const override { return expose_headers_; };
  const std::string& maxAge() const override { return max_age_; };
  const absl::optional<bool>& allowCredentials() const override { return allow_credentials_; };
  const absl::optional<bool>& allowPrivateNetworkAccess() const override {
    return allow_private_network_access_;
  };
  bool enabled() const override {
    if (config_.has_filter_enabled()) {
      const auto& filter_enabled = config_.filter_enabled();
      return loader_.snapshot().featureEnabled(filter_enabled.runtime_key(),
                                               filter_enabled.default_value());
    }
    return true;
  };
  bool shadowEnabled() const override {
    if (config_.has_shadow_enabled()) {
      const auto& shadow_enabled = config_.shadow_enabled();
      return loader_.snapshot().featureEnabled(shadow_enabled.runtime_key(),
                                               shadow_enabled.default_value());
    }
    return false;
  };

private:
  const ProtoType config_;
  Runtime::Loader& loader_;
  std::vector<Matchers::StringMatcherPtr> allow_origins_;
  const std::string allow_methods_;
  const std::string allow_headers_;
  const std::string expose_headers_;
  const std::string max_age_;
  absl::optional<bool> allow_credentials_{};
  absl::optional<bool> allow_private_network_access_{};
};
using CorsPolicyImpl = CorsPolicyImplBase<envoy::config::route::v3::CorsPolicy>;

class ConfigImpl;
/**
 * Holds all routing configuration for an entire virtual host.
 */
class VirtualHostImpl : public VirtualHost, Logger::Loggable<Logger::Id::router> {
public:
  VirtualHostImpl(
      const envoy::config::route::v3::VirtualHost& virtual_host,
      const OptionalHttpFilters& optional_http_filters, const ConfigImpl& global_route_config,
      Server::Configuration::ServerFactoryContext& factory_context, Stats::Scope& scope,
      ProtobufMessage::ValidationVisitor& validator,
      const absl::optional<Upstream::ClusterManager::ClusterInfoMaps>& validation_clusters);

  RouteConstSharedPtr getRouteFromEntries(const RouteCallback& cb,
                                          const Http::RequestHeaderMap& headers,
                                          const StreamInfo::StreamInfo& stream_info,
                                          uint64_t random_value) const;
  const VirtualCluster* virtualClusterFromEntries(const Http::HeaderMap& headers) const;
  const ConfigImpl& globalRouteConfig() const { return global_route_config_; }
  const HeaderParser& requestHeaderParser() const { return *request_headers_parser_; }
  const HeaderParser& responseHeaderParser() const { return *response_headers_parser_; }

  // Router::VirtualHost
  const CorsPolicy* corsPolicy() const override { return cors_policy_.get(); }
  Stats::StatName statName() const override { return stat_name_storage_.statName(); }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const Config& routeConfig() const override;
  const RouteSpecificFilterConfig* mostSpecificPerFilterConfig(const std::string&) const override;
  bool includeAttemptCountInRequest() const override { return include_attempt_count_in_request_; }
  bool includeAttemptCountInResponse() const override { return include_attempt_count_in_response_; }
  const std::vector<ShadowPolicyPtr>& shadowPolicies() const { return shadow_policies_; }
  const absl::optional<envoy::config::route::v3::RetryPolicy>& retryPolicy() const {
    return retry_policy_;
  }
  const absl::optional<envoy::config::route::v3::HedgePolicy>& hedgePolicy() const {
    return hedge_policy_;
  }
  uint32_t retryShadowBufferLimit() const override { return retry_shadow_buffer_limit_; }

  void traversePerFilterConfig(
      const std::string& filter_name,
      std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const override;

private:
  enum class SslRequirements { None, ExternalOnly, All };

  struct StatNameProvider {
    StatNameProvider(absl::string_view name, Stats::SymbolTable& symbol_table)
        : stat_name_storage_(name, symbol_table) {}
    Stats::StatNameManagedStorage stat_name_storage_;
  };

  struct VirtualClusterBase : public VirtualCluster {
  public:
    VirtualClusterBase(const absl::optional<std::string>& name, Stats::StatName stat_name,
                       Stats::ScopeSharedPtr&& scope, const VirtualClusterStatNames& stat_names)
        : name_(name), stat_name_(stat_name), scope_(std::move(scope)),
          stats_(generateStats(*scope_, stat_names)) {}

    // Router::VirtualCluster
    // name_ and stat_name_ are two different representations for the same string, retained in
    // memory to avoid symbol-table locks that would be needed when converting on-the-fly.
    const absl::optional<std::string>& name() const override { return name_; }
    Stats::StatName statName() const override { return stat_name_; }
    VirtualClusterStats& stats() const override { return stats_; }

  private:
    const absl::optional<std::string> name_;
    const Stats::StatName stat_name_;
    Stats::ScopeSharedPtr scope_;
    mutable VirtualClusterStats stats_;
  };

  struct VirtualClusterEntry : public StatNameProvider, public VirtualClusterBase {
    VirtualClusterEntry(const envoy::config::route::v3::VirtualCluster& virtual_cluster,
                        Stats::Scope& scope, const VirtualClusterStatNames& stat_names);
    std::vector<Http::HeaderUtility::HeaderDataPtr> headers_;
  };

  struct CatchAllVirtualCluster : public VirtualClusterBase {
    CatchAllVirtualCluster(Stats::Scope& scope, const VirtualClusterStatNames& stat_names)
        : VirtualClusterBase(absl::nullopt, stat_names.other_,
                             scope.scopeFromStatName(stat_names.other_), stat_names) {}
  };

  static const std::shared_ptr<const SslRedirectRoute> SSL_REDIRECT_ROUTE;

  const Stats::StatNameManagedStorage stat_name_storage_;
  Stats::ScopeSharedPtr vcluster_scope_;
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
  std::vector<VirtualClusterEntry> virtual_clusters_;
  SslRequirements ssl_requirements_;
  const RateLimitPolicyImpl rate_limit_policy_;
  std::vector<ShadowPolicyPtr> shadow_policies_;
  std::unique_ptr<const CorsPolicyImpl> cors_policy_;
  const ConfigImpl& global_route_config_; // See note in RouteEntryImplBase::clusterEntry() on why
                                          // raw ref to the top level config is currently safe.
  HeaderParserPtr request_headers_parser_;
  HeaderParserPtr response_headers_parser_;
  PerFilterConfigs per_filter_configs_;
  uint32_t retry_shadow_buffer_limit_{std::numeric_limits<uint32_t>::max()};
  const bool include_attempt_count_in_request_;
  const bool include_attempt_count_in_response_;
  absl::optional<envoy::config::route::v3::RetryPolicy> retry_policy_;
  absl::optional<envoy::config::route::v3::HedgePolicy> hedge_policy_;
  const CatchAllVirtualCluster virtual_cluster_catch_all_;
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_;
};

using VirtualHostSharedPtr = std::shared_ptr<VirtualHostImpl>;

/**
 * Implementation of RetryPolicy that reads from the proto route or virtual host config.
 */
class RetryPolicyImpl : public RetryPolicy {

public:
  RetryPolicyImpl(const envoy::config::route::v3::RetryPolicy& retry_policy,
                  ProtobufMessage::ValidationVisitor& validation_visitor,
                  Upstream::RetryExtensionFactoryContext& factory_context);
  RetryPolicyImpl() = default;

  // Router::RetryPolicy
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  std::chrono::milliseconds perTryIdleTimeout() const override { return per_try_idle_timeout_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }
  std::vector<Upstream::RetryHostPredicateSharedPtr> retryHostPredicates() const override;
  Upstream::RetryPrioritySharedPtr retryPriority() const override;
  absl::Span<const Upstream::RetryOptionsPredicateConstSharedPtr>
  retryOptionsPredicates() const override {
    return retry_options_predicates_;
  }
  uint32_t hostSelectionMaxAttempts() const override { return host_selection_attempts_; }
  const std::vector<uint32_t>& retriableStatusCodes() const override {
    return retriable_status_codes_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableHeaders() const override {
    return retriable_headers_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableRequestHeaders() const override {
    return retriable_request_headers_;
  }
  absl::optional<std::chrono::milliseconds> baseInterval() const override { return base_interval_; }
  absl::optional<std::chrono::milliseconds> maxInterval() const override { return max_interval_; }
  const std::vector<ResetHeaderParserSharedPtr>& resetHeaders() const override {
    return reset_headers_;
  }
  std::chrono::milliseconds resetMaxInterval() const override { return reset_max_interval_; }

private:
  std::chrono::milliseconds per_try_timeout_{0};
  std::chrono::milliseconds per_try_idle_timeout_{0};
  // We set the number of retries to 1 by default (i.e. when no route or vhost level retry policy is
  // set) so that when retries get enabled through the x-envoy-retry-on header we default to 1
  // retry.
  uint32_t num_retries_{1};
  uint32_t retry_on_{};
  // Each pair contains the name and config proto to be used to create the RetryHostPredicates
  // that should be used when with this policy.
  std::vector<std::pair<Upstream::RetryHostPredicateFactory&, ProtobufTypes::MessagePtr>>
      retry_host_predicate_configs_;
  Upstream::RetryPrioritySharedPtr retry_priority_;
  // Name and config proto to use to create the RetryPriority to use with this policy. Default
  // initialized when no RetryPriority should be used.
  std::pair<Upstream::RetryPriorityFactory*, ProtobufTypes::MessagePtr> retry_priority_config_;
  uint32_t host_selection_attempts_{1};
  std::vector<uint32_t> retriable_status_codes_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_request_headers_;
  absl::optional<std::chrono::milliseconds> base_interval_;
  absl::optional<std::chrono::milliseconds> max_interval_;
  std::vector<ResetHeaderParserSharedPtr> reset_headers_{};
  std::chrono::milliseconds reset_max_interval_{300000};
  ProtobufMessage::ValidationVisitor* validation_visitor_{};
  std::vector<Upstream::RetryOptionsPredicateConstSharedPtr> retry_options_predicates_;
};

/**
 * Implementation of ShadowPolicy that reads from the proto route config.
 */
class ShadowPolicyImpl : public ShadowPolicy {
public:
  using RequestMirrorPolicy = envoy::config::route::v3::RouteAction::RequestMirrorPolicy;
  explicit ShadowPolicyImpl(const RequestMirrorPolicy& config);

  // Router::ShadowPolicy
  const std::string& cluster() const override { return cluster_; }
  const Http::LowerCaseString& clusterHeader() const override { return cluster_header_; }
  const std::string& runtimeKey() const override { return runtime_key_; }
  const envoy::type::v3::FractionalPercent& defaultValue() const override { return default_value_; }
  bool traceSampled() const override { return trace_sampled_; }

private:
  const std::string cluster_;
  const Http::LowerCaseString cluster_header_;
  std::string runtime_key_;
  envoy::type::v3::FractionalPercent default_value_;
  bool trace_sampled_;
};

/**
 * Implementation of HedgePolicy that reads from the proto route or virtual host config.
 */
class HedgePolicyImpl : public HedgePolicy {

public:
  explicit HedgePolicyImpl(const envoy::config::route::v3::HedgePolicy& hedge_policy);
  HedgePolicyImpl();

  // Router::HedgePolicy
  uint32_t initialRequests() const override { return initial_requests_; }
  const envoy::type::v3::FractionalPercent& additionalRequestChance() const override {
    return additional_request_chance_;
  }
  bool hedgeOnPerTryTimeout() const override { return hedge_on_per_try_timeout_; }

private:
  const uint32_t initial_requests_;
  const envoy::type::v3::FractionalPercent additional_request_chance_;
  const bool hedge_on_per_try_timeout_;
};

/**
 * Implementation of Decorator that reads from the proto route decorator.
 */
class DecoratorImpl : public Decorator {
public:
  explicit DecoratorImpl(const envoy::config::route::v3::Decorator& decorator);

  // Decorator::apply
  void apply(Tracing::Span& span) const override;

  // Decorator::getOperation
  const std::string& getOperation() const override;

  // Decorator::getOperation
  bool propagate() const override;

private:
  const std::string operation_;
  const bool propagate_;
};

/**
 * Implementation of RouteTracing that reads from the proto route tracing.
 */
class RouteTracingImpl : public RouteTracing {
public:
  explicit RouteTracingImpl(const envoy::config::route::v3::Tracing& tracing);

  // Tracing::getClientSampling
  const envoy::type::v3::FractionalPercent& getClientSampling() const override;

  // Tracing::getRandomSampling
  const envoy::type::v3::FractionalPercent& getRandomSampling() const override;

  // Tracing::getOverallSampling
  const envoy::type::v3::FractionalPercent& getOverallSampling() const override;

  const Tracing::CustomTagMap& getCustomTags() const override;

private:
  envoy::type::v3::FractionalPercent client_sampling_;
  envoy::type::v3::FractionalPercent random_sampling_;
  envoy::type::v3::FractionalPercent overall_sampling_;
  Tracing::CustomTagMap custom_tags_;
};

/**
 * Implementation of InternalRedirectPolicy that reads from the proto
 * InternalRedirectPolicy of the RouteAction.
 */
class InternalRedirectPolicyImpl : public InternalRedirectPolicy {
public:
  // Constructor that enables internal redirect with policy_config controlling the configurable
  // behaviors.
  InternalRedirectPolicyImpl(const envoy::config::route::v3::InternalRedirectPolicy& policy_config,
                             ProtobufMessage::ValidationVisitor& validator,
                             absl::string_view current_route_name);
  // Default constructor that disables internal redirect.
  InternalRedirectPolicyImpl() = default;

  bool enabled() const override { return enabled_; }

  bool shouldRedirectForResponseCode(const Http::Code& response_code) const override {
    return redirect_response_codes_.contains(response_code);
  }

  std::vector<InternalRedirectPredicateSharedPtr> predicates() const override;

  uint32_t maxInternalRedirects() const override { return max_internal_redirects_; }

  bool isCrossSchemeRedirectAllowed() const override { return allow_cross_scheme_redirect_; }

private:
  absl::flat_hash_set<Http::Code> buildRedirectResponseCodes(
      const envoy::config::route::v3::InternalRedirectPolicy& policy_config) const;

  const std::string current_route_name_;
  const absl::flat_hash_set<Http::Code> redirect_response_codes_;
  const uint32_t max_internal_redirects_{1};
  const bool enabled_{false};
  const bool allow_cross_scheme_redirect_{false};

  std::vector<std::pair<InternalRedirectPredicateFactory*, ProtobufTypes::MessagePtr>>
      predicate_factories_;
};

/**
 * Base implementation for all route entries.
 */
class RouteEntryImplBase : public RouteEntryAndRoute,
                           public Matchable,
                           public DirectResponseEntry,
                           public PathMatchCriterion,
                           public std::enable_shared_from_this<RouteEntryImplBase>,
                           Logger::Loggable<Logger::Id::router> {
public:
  /**
   * @throw EnvoyException with reason if the route configuration contains any errors
   */
  RouteEntryImplBase(const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
                     const OptionalHttpFilters& optional_http_filters,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     ProtobufMessage::ValidationVisitor& validator);

  bool isDirectResponse() const { return direct_response_code_.has_value(); }

  bool isRedirect() const {
    if (!isDirectResponse()) {
      return false;
    }
    return !host_redirect_.empty() || !path_redirect_.empty() ||
           !prefix_rewrite_redirect_.empty() || regex_rewrite_redirect_ != nullptr;
  }

  bool matchRoute(const Http::RequestHeaderMap& headers, const StreamInfo::StreamInfo& stream_info,
                  uint64_t random_value) const;
  void validateClusters(const Upstream::ClusterManager::ClusterInfoMaps& cluster_info_maps) const;

  // Router::RouteEntry
  const std::string& clusterName() const override;
  const RouteStatsContextOptRef routeStatsContext() const override {
    if (route_stats_context_ != nullptr) {
      return *route_stats_context_;
    }
    return RouteStatsContextOptRef();
  }
  Http::Code clusterNotFoundResponseCode() const override {
    return cluster_not_found_response_code_;
  }
  const std::string& routeName() const override { return route_name_; }
  const CorsPolicy* corsPolicy() const override { return cors_policy_.get(); }
  void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override;
  Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                 bool do_formatting = true) const override;
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                               const StreamInfo::StreamInfo& stream_info) const override;
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                  bool do_formatting = true) const override;
  const Http::HashPolicy* hashPolicy() const override { return hash_policy_.get(); }

  const HedgePolicy& hedgePolicy() const override { return hedge_policy_; }

  const MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_criteria_.get();
  }
  const TlsContextMatchCriteria* tlsContextMatchCriteria() const override {
    return tls_context_match_criteria_.get();
  }
  Upstream::ResourcePriority priority() const override { return priority_; }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const RetryPolicy& retryPolicy() const override { return retry_policy_; }
  const InternalRedirectPolicy& internalRedirectPolicy() const override {
    return internal_redirect_policy_;
  }

  const PathMatcherSharedPtr& pathMatcher() const override { return path_matcher_; }
  const PathRewriterSharedPtr& pathRewriter() const override { return path_rewriter_; }

  uint32_t retryShadowBufferLimit() const override { return retry_shadow_buffer_limit_; }
  const std::vector<ShadowPolicyPtr>& shadowPolicies() const override { return shadow_policies_; }
  const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
    return vhost_.virtualClusterFromEntries(headers);
  }
  std::chrono::milliseconds timeout() const override { return timeout_; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return idle_timeout_; }
  bool usingNewTimeouts() const override { return using_new_timeouts_; }
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return max_stream_duration_;
  }
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderMax() const override {
    return grpc_timeout_header_max_;
  }
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderOffset() const override {
    return grpc_timeout_header_offset_;
  }
  absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override {
    return max_grpc_timeout_;
  }
  absl::optional<std::chrono::milliseconds> grpcTimeoutOffset() const override {
    return grpc_timeout_offset_;
  }
  const VirtualHost& virtualHost() const override { return vhost_; }
  bool autoHostRewrite() const override { return auto_host_rewrite_; }
  bool appendXfh() const override { return append_xfh_; }
  const std::multimap<std::string, std::string>& opaqueConfig() const override {
    return opaque_config_;
  }
  bool includeVirtualHostRateLimits() const override { return include_vh_rate_limits_; }
  const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }
  const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }
  const PathMatchCriterion& pathMatchCriterion() const override { return *this; }
  bool includeAttemptCountInRequest() const override {
    return vhost_.includeAttemptCountInRequest();
  }
  bool includeAttemptCountInResponse() const override {
    return vhost_.includeAttemptCountInResponse();
  }
  const absl::optional<ConnectConfig>& connectConfig() const override { return connect_config_; }
  const UpgradeMap& upgradeMap() const override { return upgrade_map_; }
  const EarlyDataPolicy& earlyDataPolicy() const override { return *early_data_policy_; }

  // Router::DirectResponseEntry
  std::string newPath(const Http::RequestHeaderMap& headers) const override;
  absl::string_view processRequestHost(const Http::RequestHeaderMap& headers,
                                       absl::string_view new_scheme,
                                       absl::string_view new_port) const;
  void rewritePathHeader(Http::RequestHeaderMap&, bool) const override {}
  Http::Code responseCode() const override { return direct_response_code_.value(); }
  const std::string& responseBody() const override { return direct_response_body_; }

  // Router::Route
  const DirectResponseEntry* directResponseEntry() const override;
  const RouteEntry* routeEntry() const override;
  const Decorator* decorator() const override { return decorator_.get(); }
  const RouteTracing* tracingConfig() const override { return route_tracing_.get(); }
  const RouteSpecificFilterConfig*
  mostSpecificPerFilterConfig(const std::string& name) const override {
    auto* config = per_filter_configs_.get(name);
    return config ? config : vhost_.mostSpecificPerFilterConfig(name);
  }
  void traversePerFilterConfig(
      const std::string& filter_name,
      std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const override;

  // Sanitizes the |path| before passing it to PathMatcher, if configured, this method makes the
  // path matching to ignore the path-parameters.
  absl::string_view sanitizePathBeforePathMatching(const absl::string_view path) const;

  class DynamicRouteEntry : public RouteEntryAndRoute {
  public:
    DynamicRouteEntry(const RouteEntryAndRoute* parent, const std::string& name)
        : parent_(parent), cluster_name_(name) {}

    const std::string& routeName() const override { return parent_->routeName(); }
    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    Http::Code clusterNotFoundResponseCode() const override {
      return parent_->clusterNotFoundResponseCode();
    }

    absl::optional<std::string>
    currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override {
      return parent_->currentUrlPathAfterRewrite(headers);
    }
    void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                const StreamInfo::StreamInfo& stream_info,
                                bool insert_envoy_original_path) const override {
      return parent_->finalizeRequestHeaders(headers, stream_info, insert_envoy_original_path);
    }
    Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                   bool do_formatting = true) const override {
      return parent_->requestHeaderTransforms(stream_info, do_formatting);
    }
    void finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                                 const StreamInfo::StreamInfo& stream_info) const override {
      return parent_->finalizeResponseHeaders(headers, stream_info);
    }
    Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                    bool do_formatting = true) const override {
      return parent_->responseHeaderTransforms(stream_info, do_formatting);
    }

    const CorsPolicy* corsPolicy() const override { return parent_->corsPolicy(); }
    const Http::HashPolicy* hashPolicy() const override { return parent_->hashPolicy(); }
    const HedgePolicy& hedgePolicy() const override { return parent_->hedgePolicy(); }
    Upstream::ResourcePriority priority() const override { return parent_->priority(); }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_->rateLimitPolicy(); }
    const RetryPolicy& retryPolicy() const override { return parent_->retryPolicy(); }
    const InternalRedirectPolicy& internalRedirectPolicy() const override {
      return parent_->internalRedirectPolicy();
    }
    const PathMatcherSharedPtr& pathMatcher() const override { return parent_->pathMatcher(); }
    const PathRewriterSharedPtr& pathRewriter() const override { return parent_->pathRewriter(); }
    uint32_t retryShadowBufferLimit() const override { return parent_->retryShadowBufferLimit(); }
    const std::vector<ShadowPolicyPtr>& shadowPolicies() const override {
      return parent_->shadowPolicies();
    }
    std::chrono::milliseconds timeout() const override { return parent_->timeout(); }
    absl::optional<std::chrono::milliseconds> idleTimeout() const override {
      return parent_->idleTimeout();
    }
    bool usingNewTimeouts() const override { return parent_->usingNewTimeouts(); }
    absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
      return parent_->maxStreamDuration();
    }
    absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderMax() const override {
      return parent_->grpcTimeoutHeaderMax();
    }
    absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderOffset() const override {
      return parent_->grpcTimeoutHeaderOffset();
    }
    absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override {
      return parent_->maxGrpcTimeout();
    }
    absl::optional<std::chrono::milliseconds> grpcTimeoutOffset() const override {
      return parent_->grpcTimeoutOffset();
    }
    const MetadataMatchCriteria* metadataMatchCriteria() const override {
      return parent_->metadataMatchCriteria();
    }
    const TlsContextMatchCriteria* tlsContextMatchCriteria() const override {
      return parent_->tlsContextMatchCriteria();
    }

    const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
      return parent_->virtualCluster(headers);
    }

    const std::multimap<std::string, std::string>& opaqueConfig() const override {
      return parent_->opaqueConfig();
    }

    const VirtualHost& virtualHost() const override { return parent_->virtualHost(); }
    bool autoHostRewrite() const override { return parent_->autoHostRewrite(); }
    bool appendXfh() const override { return parent_->appendXfh(); }
    bool includeVirtualHostRateLimits() const override {
      return parent_->includeVirtualHostRateLimits();
    }
    const envoy::config::core::v3::Metadata& metadata() const override {
      return parent_->metadata();
    }
    const Envoy::Config::TypedMetadata& typedMetadata() const override {
      return parent_->typedMetadata();
    }
    const PathMatchCriterion& pathMatchCriterion() const override {
      return parent_->pathMatchCriterion();
    }

    bool includeAttemptCountInRequest() const override {
      return parent_->includeAttemptCountInRequest();
    }
    bool includeAttemptCountInResponse() const override {
      return parent_->includeAttemptCountInResponse();
    }
    const absl::optional<ConnectConfig>& connectConfig() const override {
      return parent_->connectConfig();
    }
    const RouteStatsContextOptRef routeStatsContext() const override {
      return parent_->routeStatsContext();
    }
    const UpgradeMap& upgradeMap() const override { return parent_->upgradeMap(); }
    const EarlyDataPolicy& earlyDataPolicy() const override { return parent_->earlyDataPolicy(); }

    // Router::Route
    const DirectResponseEntry* directResponseEntry() const override { return nullptr; }
    const RouteEntry* routeEntry() const override { return this; }
    const Decorator* decorator() const override { return parent_->decorator(); }
    const RouteTracing* tracingConfig() const override { return parent_->tracingConfig(); }
    const RouteSpecificFilterConfig*
    mostSpecificPerFilterConfig(const std::string& name) const override {
      return parent_->mostSpecificPerFilterConfig(name);
    }
    void traversePerFilterConfig(
        const std::string& filter_name,
        std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const override {
      parent_->traversePerFilterConfig(filter_name, cb);
    };

  private:
    const RouteEntryAndRoute* parent_;
    const std::string cluster_name_;
  };

  /**
   * Route entry implementation for weighted clusters. The RouteEntryImplBase object holds
   * one or more weighted cluster objects, where each object has a back pointer to the parent
   * RouteEntryImplBase object. Almost all functions in this class forward calls back to the
   * parent, with the exception of clusterName, routeEntry, and metadataMatchCriteria.
   */
  class WeightedClusterEntry : public DynamicRouteEntry {
  public:
    WeightedClusterEntry(const RouteEntryImplBase* parent, const std::string& rutime_key,
                         Server::Configuration::ServerFactoryContext& factory_context,
                         ProtobufMessage::ValidationVisitor& validator,
                         const envoy::config::route::v3::WeightedCluster::ClusterWeight& cluster,
                         const OptionalHttpFilters& optional_http_filters);

    uint64_t clusterWeight() const {
      return loader_.snapshot().getInteger(runtime_key_, cluster_weight_);
    }

    const MetadataMatchCriteria* metadataMatchCriteria() const override {
      if (cluster_metadata_match_criteria_) {
        return cluster_metadata_match_criteria_.get();
      }
      return DynamicRouteEntry::metadataMatchCriteria();
    }

    void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                const StreamInfo::StreamInfo& stream_info,
                                bool insert_envoy_original_path) const override {
      request_headers_parser_->evaluateHeaders(headers, stream_info);
      if (!host_rewrite_.empty()) {
        headers.setHost(host_rewrite_);
      }
      DynamicRouteEntry::finalizeRequestHeaders(headers, stream_info, insert_envoy_original_path);
    }
    Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                   bool do_formatting = true) const override;
    void finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                                 const StreamInfo::StreamInfo& stream_info) const override {
      const Http::RequestHeaderMap& request_headers =
          stream_info.getRequestHeaders() == nullptr
              ? *Http::StaticEmptyHeaders::get().request_headers
              : *stream_info.getRequestHeaders();
      response_headers_parser_->evaluateHeaders(headers, request_headers, headers, stream_info);
      DynamicRouteEntry::finalizeResponseHeaders(headers, stream_info);
    }
    Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                    bool do_formatting = true) const override;

    const RouteSpecificFilterConfig*
    mostSpecificPerFilterConfig(const std::string& name) const override {
      auto* config = per_filter_configs_.get(name);
      return config ? config : DynamicRouteEntry::mostSpecificPerFilterConfig(name);
    }

    void traversePerFilterConfig(
        const std::string& filter_name,
        std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const override;

    const Http::LowerCaseString& clusterHeaderName() const { return cluster_header_name_; }

  private:
    const std::string runtime_key_;
    Runtime::Loader& loader_;
    const uint64_t cluster_weight_;
    MetadataMatchCriteriaConstPtr cluster_metadata_match_criteria_;
    HeaderParserPtr request_headers_parser_;
    HeaderParserPtr response_headers_parser_;
    PerFilterConfigs per_filter_configs_;
    const std::string host_rewrite_;
    const Http::LowerCaseString cluster_header_name_;
  };

  using WeightedClusterEntrySharedPtr = std::shared_ptr<WeightedClusterEntry>;

protected:
  const bool case_sensitive_;
  const std::string prefix_rewrite_;
  Regex::CompiledMatcherPtr regex_rewrite_;
  Regex::CompiledMatcherPtr regex_rewrite_redirect_;
  const PathMatcherSharedPtr path_matcher_;
  const PathRewriterSharedPtr path_rewriter_;
  std::string regex_rewrite_substitution_;
  std::string regex_rewrite_redirect_substitution_;
  const std::string host_rewrite_;
  bool include_vh_rate_limits_;
  absl::optional<ConnectConfig> connect_config_;

  RouteConstSharedPtr clusterEntry(const Http::RequestHeaderMap& headers,
                                   uint64_t random_value) const;

  /**
   * Returns the correct path rewrite string for this route.
   *
   * The provided container may be used to store memory backing the return value
   * therefore it must outlive any use of the return value.
   */
  const std::string& getPathRewrite(const Http::RequestHeaderMap& headers,
                                    absl::optional<std::string>& container) const;

  void finalizePathHeader(Http::RequestHeaderMap& headers, absl::string_view matched_path,
                          bool insert_envoy_original_path) const;

  absl::optional<std::string>
  currentUrlPathAfterRewriteWithMatchedPath(const Http::RequestHeaderMap& headers,
                                            absl::string_view matched_path) const;

private:
  struct RuntimeData {
    std::string fractional_runtime_key_{};
    envoy::type::v3::FractionalPercent fractional_runtime_default_{};
  };

  /**
   * Returns a vector of request header parsers which applied or will apply header transformations
   * to the request in this route.
   * @param specificity_ascend specifies whether the returned parsers will be sorted from least
   *        specific to most specific (global connection manager level header parser, virtual host
   *        level header parser and finally route-level parser.) or the reverse.
   * @return a vector of request header parsers.
   */
  absl::InlinedVector<const HeaderParser*, 3>
  getRequestHeaderParsers(bool specificity_ascend) const;

  /**
   * Returns a vector of response header parsers which applied or will apply header transformations
   * to the response in this route.
   * @param specificity_ascend specifies whether the returned parsers will be sorted from least
   *        specific to most specific (global connection manager level header parser, virtual host
   *        level header parser and finally route-level parser.) or the reverse.
   * @return a vector of request header parsers.
   */
  absl::InlinedVector<const HeaderParser*, 3>
  getResponseHeaderParsers(bool specificity_ascend) const;

  absl::optional<RuntimeData> loadRuntimeData(const envoy::config::route::v3::RouteMatch& route);

  static std::multimap<std::string, std::string>
  parseOpaqueConfig(const envoy::config::route::v3::Route& route);

  static DecoratorConstPtr parseDecorator(const envoy::config::route::v3::Route& route);

  static RouteTracingConstPtr parseRouteTracing(const envoy::config::route::v3::Route& route);

  bool evaluateRuntimeMatch(const uint64_t random_value) const;

  bool evaluateTlsContextMatch(const StreamInfo::StreamInfo& stream_info) const;

  HedgePolicyImpl
  buildHedgePolicy(const absl::optional<envoy::config::route::v3::HedgePolicy>& vhost_hedge_policy,
                   const envoy::config::route::v3::RouteAction& route_config) const;

  RetryPolicyImpl
  buildRetryPolicy(const absl::optional<envoy::config::route::v3::RetryPolicy>& vhost_retry_policy,
                   const envoy::config::route::v3::RouteAction& route_config,
                   ProtobufMessage::ValidationVisitor& validation_visitor,
                   Server::Configuration::ServerFactoryContext& factory_context) const;

  InternalRedirectPolicyImpl
  buildInternalRedirectPolicy(const envoy::config::route::v3::RouteAction& route_config,
                              ProtobufMessage::ValidationVisitor& validator,
                              absl::string_view current_route_name) const;

  PathMatcherSharedPtr buildPathMatcher(envoy::config::route::v3::Route route,
                                        ProtobufMessage::ValidationVisitor& validator) const;

  PathRewriterSharedPtr buildPathRewriter(envoy::config::route::v3::Route route,
                                          ProtobufMessage::ValidationVisitor& validator) const;

  RouteConstSharedPtr
  pickClusterViaClusterHeader(const Http::LowerCaseString& cluster_header_name,
                              const Http::HeaderMap& headers,
                              const RouteEntryAndRoute* route_selector_override) const;

  RouteConstSharedPtr pickWeightedCluster(const Http::HeaderMap& headers, uint64_t random_value,
                                          bool ignore_overflow) const;

  // Default timeout is 15s if nothing is specified in the route config.
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  std::unique_ptr<const CorsPolicyImpl> cors_policy_;
  const VirtualHostImpl& vhost_; // See note in RouteEntryImplBase::clusterEntry() on why raw ref
                                 // to virtual host is currently safe.
  const bool auto_host_rewrite_;
  const absl::optional<Http::LowerCaseString> auto_host_rewrite_header_;
  const Regex::CompiledMatcherPtr host_rewrite_path_regex_;
  const std::string host_rewrite_path_regex_substitution_;
  const bool append_xfh_;
  const std::string cluster_name_;
  RouteStatsContextPtr route_stats_context_;
  const Http::LowerCaseString cluster_header_name_;
  ClusterSpecifierPluginSharedPtr cluster_specifier_plugin_;
  const Http::Code cluster_not_found_response_code_;
  const std::chrono::milliseconds timeout_;
  const absl::optional<std::chrono::milliseconds> idle_timeout_;
  const absl::optional<std::chrono::milliseconds> max_stream_duration_;
  const absl::optional<std::chrono::milliseconds> grpc_timeout_header_max_;
  const absl::optional<std::chrono::milliseconds> grpc_timeout_header_offset_;
  const absl::optional<std::chrono::milliseconds> max_grpc_timeout_;
  const absl::optional<std::chrono::milliseconds> grpc_timeout_offset_;
  Runtime::Loader& loader_;
  const absl::optional<RuntimeData> runtime_;
  const std::string scheme_redirect_;
  const std::string host_redirect_;
  const std::string port_redirect_;
  const std::string path_redirect_;
  const bool path_redirect_has_query_;
  const bool https_redirect_;
  const bool using_new_timeouts_;
  const std::string prefix_rewrite_redirect_;
  const bool strip_query_;
  const HedgePolicyImpl hedge_policy_;
  const RetryPolicyImpl retry_policy_;
  const InternalRedirectPolicyImpl internal_redirect_policy_;
  const RateLimitPolicyImpl rate_limit_policy_;
  std::vector<ShadowPolicyPtr> shadow_policies_;
  const Upstream::ResourcePriority priority_;
  std::vector<Http::HeaderUtility::HeaderDataPtr> config_headers_;
  std::vector<ConfigUtility::QueryParameterMatcherPtr> config_query_parameters_;
  std::vector<WeightedClusterEntrySharedPtr> weighted_clusters_;

  UpgradeMap upgrade_map_;
  uint64_t total_cluster_weight_;
  std::unique_ptr<const Http::HashPolicyImpl> hash_policy_;
  MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  TlsContextMatchCriteriaConstPtr tls_context_match_criteria_;
  HeaderParserPtr request_headers_parser_;
  HeaderParserPtr response_headers_parser_;
  uint32_t retry_shadow_buffer_limit_{std::numeric_limits<uint32_t>::max()};
  envoy::config::core::v3::Metadata metadata_;
  Envoy::Config::TypedMetadataImpl<HttpRouteTypedMetadataFactory> typed_metadata_;
  const bool match_grpc_;
  const std::vector<Envoy::Matchers::MetadataMatcher> dynamic_metadata_;

  // TODO(danielhochman): refactor multimap into unordered_map since JSON is unordered map.
  const std::multimap<std::string, std::string> opaque_config_;

  const DecoratorConstPtr decorator_;
  const RouteTracingConstPtr route_tracing_;
  const absl::optional<Http::Code> direct_response_code_;
  std::string direct_response_body_;
  PerFilterConfigs per_filter_configs_;
  const std::string route_name_;
  TimeSource& time_source_;
  const std::string random_value_header_name_;
  EarlyDataPolicyPtr early_data_policy_;
};

/**
 * Route entry implementation for uri template match based routing.
 */
class UriTemplateMatcherRouteEntryImpl : public RouteEntryImplBase {
public:
  UriTemplateMatcherRouteEntryImpl(const VirtualHostImpl& vhost,
                                   const envoy::config::route::v3::Route& route,
                                   const OptionalHttpFilters& optional_http_filters,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   ProtobufMessage::ValidationVisitor& validator);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return uri_template_; }
  PathMatchType matchType() const override { return PathMatchType::Template; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::RequestHeaderMap& headers,
                         bool insert_envoy_original_path) const override;

  // Router::RouteEntry
  absl::optional<std::string>
  currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override;

private:
  const std::string uri_template_;
};

/**
 * Route entry implementation for prefix path match routing.
 */
class PrefixRouteEntryImpl : public RouteEntryImplBase {
public:
  PrefixRouteEntryImpl(const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
                       const OptionalHttpFilters& optional_http_filters,
                       Server::Configuration::ServerFactoryContext& factory_context,
                       ProtobufMessage::ValidationVisitor& validator);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return prefix_; }
  PathMatchType matchType() const override { return PathMatchType::Prefix; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::RequestHeaderMap& headers,
                         bool insert_envoy_original_path) const override;

  // Router::RouteEntry
  absl::optional<std::string>
  currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override;

private:
  const std::string prefix_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};

/**
 * Route entry implementation for exact path match routing.
 */
class PathRouteEntryImpl : public RouteEntryImplBase {
public:
  PathRouteEntryImpl(const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
                     const OptionalHttpFilters& optional_http_filters,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     ProtobufMessage::ValidationVisitor& validator);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return path_; }
  PathMatchType matchType() const override { return PathMatchType::Exact; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::RequestHeaderMap& headers,
                         bool insert_envoy_original_path) const override;

  // Router::RouteEntry
  absl::optional<std::string>
  currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override;

private:
  const std::string path_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};

/**
 * Route entry implementation for regular expression match routing.
 */
class RegexRouteEntryImpl : public RouteEntryImplBase {
public:
  RegexRouteEntryImpl(const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
                      const OptionalHttpFilters& optional_http_filters,
                      Server::Configuration::ServerFactoryContext& factory_context,
                      ProtobufMessage::ValidationVisitor& validator);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return regex_str_; }
  PathMatchType matchType() const override { return PathMatchType::Regex; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::RequestHeaderMap& headers,
                         bool insert_envoy_original_path) const override;

  // Router::RouteEntry
  absl::optional<std::string>
  currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override;

private:
  const std::string regex_str_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};

/**
 * Route entry implementation for CONNECT requests.
 */
class ConnectRouteEntryImpl : public RouteEntryImplBase {
public:
  ConnectRouteEntryImpl(const VirtualHostImpl& vhost, const envoy::config::route::v3::Route& route,
                        const OptionalHttpFilters& optional_http_filters,
                        Server::Configuration::ServerFactoryContext& factory_context,
                        ProtobufMessage::ValidationVisitor& validator);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return EMPTY_STRING; }
  PathMatchType matchType() const override { return PathMatchType::None; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::RequestHeaderMap&, bool) const override;

  // Router::RouteEntry
  absl::optional<std::string>
  currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override;

  bool supportsPathlessHeaders() const override { return true; }
};

/**
 * Route entry implementation for path separated prefix match routing.
 */
class PathSeparatedPrefixRouteEntryImpl : public RouteEntryImplBase {
public:
  PathSeparatedPrefixRouteEntryImpl(const VirtualHostImpl& vhost,
                                    const envoy::config::route::v3::Route& route,
                                    const OptionalHttpFilters& optional_http_filters,
                                    Server::Configuration::ServerFactoryContext& factory_context,
                                    ProtobufMessage::ValidationVisitor& validator);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return prefix_; }
  PathMatchType matchType() const override { return PathMatchType::PathSeparatedPrefix; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::RequestHeaderMap& headers,
                         bool insert_envoy_original_path) const override;

  // Router::RouteEntry
  absl::optional<std::string>
  currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override;

private:
  const std::string prefix_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};

// Contextual information used to construct the route actions for a match tree.
struct RouteActionContext {
  const VirtualHostImpl& vhost;
  const OptionalHttpFilters& optional_http_filters;
  Server::Configuration::ServerFactoryContext& factory_context;
};

// Action used with the matching tree to specify route to use for an incoming stream.
class RouteMatchAction : public Matcher::ActionBase<envoy::config::route::v3::Route> {
public:
  explicit RouteMatchAction(RouteEntryImplBaseConstSharedPtr route) : route_(std::move(route)) {}

  RouteEntryImplBaseConstSharedPtr route() const { return route_; }

private:
  const RouteEntryImplBaseConstSharedPtr route_;
};

// Registered factory for RouteMatchAction.
class RouteMatchActionFactory : public Matcher::ActionFactory<RouteActionContext> {
public:
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, RouteActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;
  std::string name() const override { return "route"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::route::v3::Route>();
  }
};

/**
 * Wraps the route configuration which matches an incoming request headers to a backend cluster.
 * This is split out mainly to help with unit testing.
 */
class RouteMatcher {
public:
  RouteMatcher(const envoy::config::route::v3::RouteConfiguration& config,
               const OptionalHttpFilters& optional_http_filters,
               const ConfigImpl& global_http_config,
               Server::Configuration::ServerFactoryContext& factory_context,
               ProtobufMessage::ValidationVisitor& validator, bool validate_clusters);

  RouteConstSharedPtr route(const RouteCallback& cb, const Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info, uint64_t random_value) const;

  const VirtualHostImpl* findVirtualHost(const Http::RequestHeaderMap& headers) const;

private:
  using WildcardVirtualHosts =
      std::map<int64_t, absl::node_hash_map<std::string, VirtualHostSharedPtr>, std::greater<>>;
  using SubstringFunction = std::function<std::string(const std::string&, int)>;
  const VirtualHostImpl* findWildcardVirtualHost(const std::string& host,
                                                 const WildcardVirtualHosts& wildcard_virtual_hosts,
                                                 SubstringFunction substring_function) const;
  bool ignorePortInHostMatching() const { return ignore_port_in_host_matching_; }

  Stats::ScopeSharedPtr vhost_scope_;
  absl::node_hash_map<std::string, VirtualHostSharedPtr> virtual_hosts_;
  // std::greater as a minor optimization to iterate from more to less specific
  //
  // A note on using an unordered_map versus a vector of (string, VirtualHostSharedPtr) pairs:
  //
  // Based on local benchmarks, each vector entry costs around 20ns for recall and (string)
  // comparison with a fixed cost of about 25ns. For unordered_map, the empty map costs about 65ns
  // and climbs to about 110ns once there are any entries.
  //
  // The break-even is 4 entries.
  WildcardVirtualHosts wildcard_virtual_host_suffixes_;
  WildcardVirtualHosts wildcard_virtual_host_prefixes_;

  VirtualHostSharedPtr default_virtual_host_;
  const bool ignore_port_in_host_matching_{false};
};

/**
 * Implementation of Config that reads from a proto file.
 */
class ConfigImpl : public Config {
public:
  ConfigImpl(const envoy::config::route::v3::RouteConfiguration& config,
             const OptionalHttpFilters& optional_http_filters,
             Server::Configuration::ServerFactoryContext& factory_context,
             ProtobufMessage::ValidationVisitor& validator, bool validate_clusters_default);

  const HeaderParser& requestHeaderParser() const { return *request_headers_parser_; };
  const HeaderParser& responseHeaderParser() const { return *response_headers_parser_; };

  bool virtualHostExists(const Http::RequestHeaderMap& headers) const {
    return route_matcher_->findVirtualHost(headers) != nullptr;
  }

  const RouteSpecificFilterConfig* perFilterConfig(const std::string& name) const {
    return per_filter_configs_.get(name);
  }

  // Router::Config
  RouteConstSharedPtr route(const Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info,
                            uint64_t random_value) const override {
    return route(nullptr, headers, stream_info, random_value);
  }

  RouteConstSharedPtr route(const RouteCallback& cb, const Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info,
                            uint64_t random_value) const override;

  const std::list<Http::LowerCaseString>& internalOnlyHeaders() const override {
    return internal_only_headers_;
  }

  const std::string& name() const override { return name_; }

  bool usesVhds() const override { return uses_vhds_; }

  bool mostSpecificHeaderMutationsWins() const override {
    return most_specific_header_mutations_wins_;
  }

  uint32_t maxDirectResponseBodySizeBytes() const override {
    return max_direct_response_body_size_bytes_;
  }

  const std::vector<ShadowPolicyPtr>& shadowPolicies() const { return shadow_policies_; }

  ClusterSpecifierPluginSharedPtr clusterSpecifierPlugin(absl::string_view provider) const;
  bool ignorePathParametersInPathMatching() const {
    return ignore_path_parameters_in_path_matching_;
  }

private:
  std::unique_ptr<RouteMatcher> route_matcher_;
  std::list<Http::LowerCaseString> internal_only_headers_;
  HeaderParserPtr request_headers_parser_;
  HeaderParserPtr response_headers_parser_;
  const std::string name_;
  Stats::SymbolTable& symbol_table_;
  const bool uses_vhds_;
  const bool most_specific_header_mutations_wins_;
  const uint32_t max_direct_response_body_size_bytes_;
  std::vector<ShadowPolicyPtr> shadow_policies_;
  // Cluster specifier plugins/providers.
  absl::flat_hash_map<std::string, ClusterSpecifierPluginSharedPtr> cluster_specifier_plugins_;
  const bool ignore_path_parameters_in_path_matching_;
  PerFilterConfigs per_filter_configs_;
};

/**
 * Implementation of Config that is empty.
 */
class NullConfigImpl : public Config {
public:
  // Router::Config
  RouteConstSharedPtr route(const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                            uint64_t) const override {
    return nullptr;
  }

  RouteConstSharedPtr route(const RouteCallback&, const Http::RequestHeaderMap&,
                            const StreamInfo::StreamInfo&, uint64_t) const override {
    return nullptr;
  }

  const std::list<Http::LowerCaseString>& internalOnlyHeaders() const override {
    return internal_only_headers_;
  }

  const std::string& name() const override { return name_; }
  bool usesVhds() const override { return false; }
  bool mostSpecificHeaderMutationsWins() const override { return false; }
  uint32_t maxDirectResponseBodySizeBytes() const override { return 0; }

private:
  std::list<Http::LowerCaseString> internal_only_headers_;
  const std::string name_;
};

} // namespace Router
} // namespace Envoy
