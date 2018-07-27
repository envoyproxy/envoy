#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_utility.h"
#include "common/router/config_utility.h"
#include "common/router/header_formatter.h"
#include "common/router/header_parser.h"
#include "common/router/metadatamatchcriteria_impl.h"
#include "common/router/router_ratelimit.h"
#include "common/tcp_proxy/tcp_proxy.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

/**
 * Base interface for something that matches a header.
 */
class Matchable {
public:
  virtual ~Matchable() {}

  /**
   * See if this object matches the incoming headers.
   * @param headers supplies the headers to match.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return true if input headers match this object.
   */
  virtual RouteConstSharedPtr matches(const Http::HeaderMap& headers,
                                      uint64_t random_value) const PURE;
};

class PerFilterConfigs {
public:
  PerFilterConfigs(const Protobuf::Map<ProtobufTypes::String, ProtobufWkt::Struct>& configs,
                   Server::Configuration::FactoryContext& factory_context);

  const RouteSpecificFilterConfig* get(const std::string& name) const;

private:
  std::unordered_map<std::string, RouteSpecificFilterConfigConstSharedPtr> configs_;
};

class RouteEntryImplBase;
typedef std::shared_ptr<const RouteEntryImplBase> RouteEntryImplBaseConstSharedPtr;

/**
 * Direct response entry that does an SSL redirect.
 */
class SslRedirector : public DirectResponseEntry {
public:
  // Router::DirectResponseEntry
  void finalizeResponseHeaders(Http::HeaderMap&, const RequestInfo::RequestInfo&) const override {}
  std::string newPath(const Http::HeaderMap& headers) const override;
  void rewritePathHeader(Http::HeaderMap&, bool) const override {}
  Http::Code responseCode() const override { return Http::Code::MovedPermanently; }
  const std::string& responseBody() const override { return EMPTY_STRING; }
};

class SslRedirectRoute : public Route {
public:
  // Router::Route
  const DirectResponseEntry* directResponseEntry() const override { return &SSL_REDIRECTOR; }
  const RouteEntry* routeEntry() const override { return nullptr; }
  const Decorator* decorator() const override { return nullptr; }
  const RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override {
    return nullptr;
  }

private:
  static const SslRedirector SSL_REDIRECTOR;
};

/**
 * Implementation of CorsPolicy that reads from the proto route and virtual host config.
 */
class CorsPolicyImpl : public CorsPolicy {
public:
  CorsPolicyImpl(const envoy::api::v2::route::CorsPolicy& config);

  // Router::CorsPolicy
  const std::list<std::string>& allowOrigins() const override { return allow_origin_; };
  const std::list<std::regex>& allowOriginRegexes() const override { return allow_origin_regex_; }
  const std::string& allowMethods() const override { return allow_methods_; };
  const std::string& allowHeaders() const override { return allow_headers_; };
  const std::string& exposeHeaders() const override { return expose_headers_; };
  const std::string& maxAge() const override { return max_age_; };
  const absl::optional<bool>& allowCredentials() const override { return allow_credentials_; };
  bool enabled() const override { return enabled_; };

private:
  std::list<std::string> allow_origin_;
  std::list<std::regex> allow_origin_regex_;
  std::string allow_methods_;
  std::string allow_headers_;
  std::string expose_headers_;
  std::string max_age_{};
  absl::optional<bool> allow_credentials_{};
  bool enabled_;
};

class ConfigImpl;
/**
 * Holds all routing configuration for an entire virtual host.
 */
class VirtualHostImpl : public VirtualHost {
public:
  VirtualHostImpl(const envoy::api::v2::route::VirtualHost& virtual_host,
                  const ConfigImpl& global_route_config,
                  Server::Configuration::FactoryContext& factory_context, bool validate_clusters);

  RouteConstSharedPtr getRouteFromEntries(const Http::HeaderMap& headers,
                                          uint64_t random_value) const;
  const VirtualCluster* virtualClusterFromEntries(const Http::HeaderMap& headers) const;
  const ConfigImpl& globalRouteConfig() const { return global_route_config_; }
  const HeaderParser& requestHeaderParser() const { return *request_headers_parser_; };
  const HeaderParser& responseHeaderParser() const { return *response_headers_parser_; };

  // Router::VirtualHost
  const CorsPolicy* corsPolicy() const override { return cors_policy_.get(); }
  const std::string& name() const override { return name_; }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const Config& routeConfig() const override;
  const RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override;

private:
  enum class SslRequirements { NONE, EXTERNAL_ONLY, ALL };

  struct VirtualClusterEntry : public VirtualCluster {
    VirtualClusterEntry(const envoy::api::v2::route::VirtualCluster& virtual_cluster);

    // Router::VirtualCluster
    const std::string& name() const override { return name_; }

    std::regex pattern_;
    absl::optional<std::string> method_;
    std::string name_;
  };

  struct CatchAllVirtualCluster : public VirtualCluster {
    // Router::VirtualCluster
    const std::string& name() const override { return name_; }

    std::string name_{"other"};
  };

  static const CatchAllVirtualCluster VIRTUAL_CLUSTER_CATCH_ALL;
  static const std::shared_ptr<const SslRedirectRoute> SSL_REDIRECT_ROUTE;

  const std::string name_;
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
  std::vector<VirtualClusterEntry> virtual_clusters_;
  SslRequirements ssl_requirements_;
  const RateLimitPolicyImpl rate_limit_policy_;
  std::unique_ptr<const CorsPolicyImpl> cors_policy_;
  const ConfigImpl& global_route_config_; // See note in RouteEntryImplBase::clusterEntry() on why
                                          // raw ref to the top level config is currently safe.
  HeaderParserPtr request_headers_parser_;
  HeaderParserPtr response_headers_parser_;
  PerFilterConfigs per_filter_configs_;
};

typedef std::shared_ptr<VirtualHostImpl> VirtualHostSharedPtr;

/**
 * Implementation of RetryPolicy that reads from the proto route config.
 */
class RetryPolicyImpl : public RetryPolicy {
public:
  RetryPolicyImpl(const envoy::api::v2::route::RouteAction& config);

  // Router::RetryPolicy
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }

private:
  std::chrono::milliseconds per_try_timeout_{0};
  uint32_t num_retries_{};
  uint32_t retry_on_{};
};

/**
 * Implementation of ShadowPolicy that reads from the proto route config.
 */
class ShadowPolicyImpl : public ShadowPolicy {
public:
  ShadowPolicyImpl(const envoy::api::v2::route::RouteAction& config);

  // Router::ShadowPolicy
  const std::string& cluster() const override { return cluster_; }
  const std::string& runtimeKey() const override { return runtime_key_; }

private:
  std::string cluster_;
  std::string runtime_key_;
};

/**
 * Implementation of HashPolicy that reads from the proto route config and only currently supports
 * hashing on an HTTP header.
 */
class HashPolicyImpl : public HashPolicy {
public:
  HashPolicyImpl(const Protobuf::RepeatedPtrField<envoy::api::v2::route::RouteAction::HashPolicy>&
                     hash_policy);

  // Router::HashPolicy
  absl::optional<uint64_t> generateHash(const Network::Address::Instance* downstream_addr,
                                        const Http::HeaderMap& headers,
                                        const AddCookieCallback add_cookie) const override;

  class HashMethod {
  public:
    virtual ~HashMethod() {}
    virtual absl::optional<uint64_t> evaluate(const Network::Address::Instance* downstream_addr,
                                              const Http::HeaderMap& headers,
                                              const AddCookieCallback add_cookie) const PURE;
  };

  typedef std::unique_ptr<HashMethod> HashMethodPtr;

private:
  std::vector<HashMethodPtr> hash_impls_;
};

/**
 * Implementation of Decorator that reads from the proto route decorator.
 */
class DecoratorImpl : public Decorator {
public:
  DecoratorImpl(const envoy::api::v2::route::Decorator& decorator);

  // Decorator::apply
  void apply(Tracing::Span& span) const override;

  // Decorator::getOperation
  const std::string& getOperation() const override;

private:
  const std::string operation_;
};

/**
 * Base implementation for all route entries.
 */
class RouteEntryImplBase : public RouteEntry,
                           public Matchable,
                           public DirectResponseEntry,
                           public Route,
                           public PathMatchCriterion,
                           public std::enable_shared_from_this<RouteEntryImplBase> {
public:
  /**
   * @throw EnvoyException with reason if the route configuration contains any errors
   */
  RouteEntryImplBase(const VirtualHostImpl& vhost, const envoy::api::v2::route::Route& route,
                     Server::Configuration::FactoryContext& factory_context);

  bool isDirectResponse() const { return direct_response_code_.has_value(); }

  bool isRedirect() const {
    if (!isDirectResponse()) {
      return false;
    }
    return !host_redirect_.empty() || !path_redirect_.empty() || !prefix_rewrite_redirect_.empty();
  }

  bool matchRoute(const Http::HeaderMap& headers, uint64_t random_value) const;
  void validateClusters(Upstream::ClusterManager& cm) const;

  // Router::RouteEntry
  const std::string& clusterName() const override;
  Http::Code clusterNotFoundResponseCode() const override {
    return cluster_not_found_response_code_;
  }
  const CorsPolicy* corsPolicy() const override { return cors_policy_.get(); }
  void finalizeRequestHeaders(Http::HeaderMap& headers,
                              const RequestInfo::RequestInfo& request_info,
                              bool insert_envoy_original_path) const override;
  void finalizeResponseHeaders(Http::HeaderMap& headers,
                               const RequestInfo::RequestInfo& request_info) const override;
  const HashPolicy* hashPolicy() const override { return hash_policy_.get(); }

  const MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_criteria_.get();
  }
  Upstream::ResourcePriority priority() const override { return priority_; }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const RetryPolicy& retryPolicy() const override { return retry_policy_; }
  const ShadowPolicy& shadowPolicy() const override { return shadow_policy_; }
  const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
    return vhost_.virtualClusterFromEntries(headers);
  }
  std::chrono::milliseconds timeout() const override { return timeout_; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return idle_timeout_; }
  absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override {
    return max_grpc_timeout_;
  }
  const VirtualHost& virtualHost() const override { return vhost_; }
  bool autoHostRewrite() const override { return auto_host_rewrite_; }
  bool useOldStyleWebSocket() const override { return websocket_config_ != nullptr; }
  Http::WebSocketProxyPtr
  createWebSocketProxy(Http::HeaderMap& request_headers, RequestInfo::RequestInfo& request_info,
                       Http::WebSocketProxyCallbacks& callbacks,
                       Upstream::ClusterManager& cluster_manager,
                       Network::ReadFilterCallbacks* read_callbacks) const override;
  const std::multimap<std::string, std::string>& opaqueConfig() const override {
    return opaque_config_;
  }
  bool includeVirtualHostRateLimits() const override { return include_vh_rate_limits_; }
  const envoy::api::v2::core::Metadata& metadata() const override { return metadata_; }
  const PathMatchCriterion& pathMatchCriterion() const override { return *this; }

  // Router::DirectResponseEntry
  std::string newPath(const Http::HeaderMap& headers) const override;
  void rewritePathHeader(Http::HeaderMap&, bool) const override {}
  Http::Code responseCode() const override { return direct_response_code_.value(); }
  const std::string& responseBody() const override { return direct_response_body_; }

  // Router::Route
  const DirectResponseEntry* directResponseEntry() const override;
  const RouteEntry* routeEntry() const override;
  const Decorator* decorator() const override { return decorator_.get(); }
  const RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override;

protected:
  const bool case_sensitive_;
  const std::string prefix_rewrite_;
  const std::string host_rewrite_;
  bool include_vh_rate_limits_;

  RouteConstSharedPtr clusterEntry(const Http::HeaderMap& headers, uint64_t random_value) const;

  /**
   * returns the correct path rewrite string for this route.
   */
  const std::string& getPathRewrite() const {
    return (isRedirect()) ? prefix_rewrite_redirect_ : prefix_rewrite_;
  }

  void finalizePathHeader(Http::HeaderMap& headers, const std::string& matched_path,
                          bool insert_envoy_original_path) const;

private:
  struct RuntimeData {
    std::string key_{};
    uint64_t default_{};
  };

  class DynamicRouteEntry : public RouteEntry, public Route {
  public:
    DynamicRouteEntry(const RouteEntryImplBase* parent, const std::string& name)
        : parent_(parent), cluster_name_(name) {}

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    Http::Code clusterNotFoundResponseCode() const override {
      return parent_->clusterNotFoundResponseCode();
    }

    void finalizeRequestHeaders(Http::HeaderMap& headers,
                                const RequestInfo::RequestInfo& request_info,
                                bool insert_envoy_original_path) const override {
      return parent_->finalizeRequestHeaders(headers, request_info, insert_envoy_original_path);
    }
    void finalizeResponseHeaders(Http::HeaderMap& headers,
                                 const RequestInfo::RequestInfo& request_info) const override {
      return parent_->finalizeResponseHeaders(headers, request_info);
    }

    const CorsPolicy* corsPolicy() const override { return parent_->corsPolicy(); }
    const HashPolicy* hashPolicy() const override { return parent_->hashPolicy(); }
    Upstream::ResourcePriority priority() const override { return parent_->priority(); }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_->rateLimitPolicy(); }
    const RetryPolicy& retryPolicy() const override { return parent_->retryPolicy(); }
    const ShadowPolicy& shadowPolicy() const override { return parent_->shadowPolicy(); }
    std::chrono::milliseconds timeout() const override { return parent_->timeout(); }
    absl::optional<std::chrono::milliseconds> idleTimeout() const override {
      return parent_->idleTimeout();
    }
    absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override {
      return parent_->maxGrpcTimeout();
    }
    const MetadataMatchCriteria* metadataMatchCriteria() const override {
      return parent_->metadataMatchCriteria();
    }

    const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
      return parent_->virtualCluster(headers);
    }

    const std::multimap<std::string, std::string>& opaqueConfig() const override {
      return parent_->opaqueConfig();
    }

    const VirtualHost& virtualHost() const override { return parent_->virtualHost(); }
    bool autoHostRewrite() const override { return parent_->autoHostRewrite(); }
    bool useOldStyleWebSocket() const override { return parent_->useOldStyleWebSocket(); }
    Http::WebSocketProxyPtr
    createWebSocketProxy(Http::HeaderMap& request_headers, RequestInfo::RequestInfo& request_info,
                         Http::WebSocketProxyCallbacks& callbacks,
                         Upstream::ClusterManager& cluster_manager,
                         Network::ReadFilterCallbacks* read_callbacks) const override {
      return parent_->createWebSocketProxy(request_headers, request_info, callbacks,
                                           cluster_manager, read_callbacks);
    }
    bool includeVirtualHostRateLimits() const override {
      return parent_->includeVirtualHostRateLimits();
    }
    const envoy::api::v2::core::Metadata& metadata() const override { return parent_->metadata(); }
    const PathMatchCriterion& pathMatchCriterion() const override {
      return parent_->pathMatchCriterion();
    }

    // Router::Route
    const DirectResponseEntry* directResponseEntry() const override { return nullptr; }
    const RouteEntry* routeEntry() const override { return this; }
    const Decorator* decorator() const override { return parent_->decorator(); }

    const RouteSpecificFilterConfig* perFilterConfig(const std::string& name) const override {
      return parent_->perFilterConfig(name);
    };

  private:
    const RouteEntryImplBase* parent_;
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
    WeightedClusterEntry(const RouteEntryImplBase* parent, const std::string rutime_key,
                         Server::Configuration::FactoryContext& factory_context,
                         const envoy::api::v2::route::WeightedCluster_ClusterWeight& cluster);

    uint64_t clusterWeight() const {
      return loader_.snapshot().getInteger(runtime_key_, cluster_weight_);
    }

    const MetadataMatchCriteria* metadataMatchCriteria() const override {
      if (cluster_metadata_match_criteria_) {
        return cluster_metadata_match_criteria_.get();
      }
      return DynamicRouteEntry::metadataMatchCriteria();
    }

    void finalizeRequestHeaders(Http::HeaderMap& headers,
                                const RequestInfo::RequestInfo& request_info,
                                bool insert_envoy_original_path) const override {
      request_headers_parser_->evaluateHeaders(headers, request_info);
      DynamicRouteEntry::finalizeRequestHeaders(headers, request_info, insert_envoy_original_path);
    }
    void finalizeResponseHeaders(Http::HeaderMap& headers,
                                 const RequestInfo::RequestInfo& request_info) const override {
      response_headers_parser_->evaluateHeaders(headers, request_info);
      DynamicRouteEntry::finalizeResponseHeaders(headers, request_info);
    }

    const RouteSpecificFilterConfig* perFilterConfig(const std::string& name) const override;

  private:
    const std::string runtime_key_;
    Runtime::Loader& loader_;
    const uint64_t cluster_weight_;
    MetadataMatchCriteriaConstPtr cluster_metadata_match_criteria_;
    HeaderParserPtr request_headers_parser_;
    HeaderParserPtr response_headers_parser_;
    PerFilterConfigs per_filter_configs_;
  };

  typedef std::shared_ptr<WeightedClusterEntry> WeightedClusterEntrySharedPtr;

  static absl::optional<RuntimeData>
  loadRuntimeData(const envoy::api::v2::route::RouteMatch& route);

  static std::multimap<std::string, std::string>
  parseOpaqueConfig(const envoy::api::v2::route::Route& route);

  static DecoratorConstPtr parseDecorator(const envoy::api::v2::route::Route& route);

  // Default timeout is 15s if nothing is specified in the route config.
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  std::unique_ptr<const CorsPolicyImpl> cors_policy_;
  const VirtualHostImpl& vhost_; // See note in RouteEntryImplBase::clusterEntry() on why raw ref
                                 // to virtual host is currently safe.
  const bool auto_host_rewrite_;
  const TcpProxy::ConfigSharedPtr websocket_config_;
  const std::string cluster_name_;
  const Http::LowerCaseString cluster_header_name_;
  const Http::Code cluster_not_found_response_code_;
  const std::chrono::milliseconds timeout_;
  const absl::optional<std::chrono::milliseconds> idle_timeout_;
  const absl::optional<std::chrono::milliseconds> max_grpc_timeout_;
  const absl::optional<RuntimeData> runtime_;
  Runtime::Loader& loader_;
  const std::string host_redirect_;
  const std::string path_redirect_;
  const bool https_redirect_;
  const std::string prefix_rewrite_redirect_;
  const bool strip_query_;
  const RetryPolicyImpl retry_policy_;
  const RateLimitPolicyImpl rate_limit_policy_;
  const ShadowPolicyImpl shadow_policy_;
  const Upstream::ResourcePriority priority_;
  std::vector<Http::HeaderUtility::HeaderData> config_headers_;
  std::vector<ConfigUtility::QueryParameterMatcher> config_query_parameters_;
  std::vector<WeightedClusterEntrySharedPtr> weighted_clusters_;
  const uint64_t total_cluster_weight_;
  std::unique_ptr<const HashPolicyImpl> hash_policy_;
  MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  HeaderParserPtr route_action_request_headers_parser_;
  HeaderParserPtr route_action_response_headers_parser_;
  HeaderParserPtr request_headers_parser_;
  HeaderParserPtr response_headers_parser_;
  envoy::api::v2::core::Metadata metadata_;

  // TODO(danielhochman): refactor multimap into unordered_map since JSON is unordered map.
  const std::multimap<std::string, std::string> opaque_config_;

  const DecoratorConstPtr decorator_;
  const absl::optional<Http::Code> direct_response_code_;
  std::string direct_response_body_;
  PerFilterConfigs per_filter_configs_;
};

/**
 * Route entry implementation for prefix path match routing.
 */
class PrefixRouteEntryImpl : public RouteEntryImplBase {
public:
  PrefixRouteEntryImpl(const VirtualHostImpl& vhost, const envoy::api::v2::route::Route& route,
                       Server::Configuration::FactoryContext& factory_context);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return prefix_; }
  PathMatchType matchType() const override { return PathMatchType::Prefix; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::HeaderMap& headers, bool insert_envoy_original_path) const override;

private:
  const std::string prefix_;
};

/**
 * Route entry implementation for exact path match routing.
 */
class PathRouteEntryImpl : public RouteEntryImplBase {
public:
  PathRouteEntryImpl(const VirtualHostImpl& vhost, const envoy::api::v2::route::Route& route,
                     Server::Configuration::FactoryContext& factory_context);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return path_; }
  PathMatchType matchType() const override { return PathMatchType::Exact; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::HeaderMap& headers, bool insert_envoy_original_path) const override;

private:
  const std::string path_;
};

/**
 * Route entry implementation for regular expression match routing.
 */
class RegexRouteEntryImpl : public RouteEntryImplBase {
public:
  RegexRouteEntryImpl(const VirtualHostImpl& vhost, const envoy::api::v2::route::Route& route,
                      Server::Configuration::FactoryContext& factory_context);

  // Router::PathMatchCriterion
  const std::string& matcher() const override { return regex_str_; }
  PathMatchType matchType() const override { return PathMatchType::Regex; }

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

  // Router::DirectResponseEntry
  void rewritePathHeader(Http::HeaderMap& headers, bool insert_envoy_original_path) const override;

private:
  const std::regex regex_;
  const std::string regex_str_;
};

/**
 * Wraps the route configuration which matches an incoming request headers to a backend cluster.
 * This is split out mainly to help with unit testing.
 */
class RouteMatcher {
public:
  RouteMatcher(const envoy::api::v2::RouteConfiguration& config,
               const ConfigImpl& global_http_config,
               Server::Configuration::FactoryContext& factory_context, bool validate_clusters);

  RouteConstSharedPtr route(const Http::HeaderMap& headers, uint64_t random_value) const;

private:
  const VirtualHostImpl* findVirtualHost(const Http::HeaderMap& headers) const;
  const VirtualHostImpl* findWildcardVirtualHost(const std::string& host) const;

  std::unordered_map<std::string, VirtualHostSharedPtr> virtual_hosts_;
  // std::greater as a minor optimization to iterate from more to less specific
  //
  // A note on using an unordered_map versus a vector of (string, VirtualHostSharedPtr) pairs:
  //
  // Based on local benchmarks, each vector entry costs around 20ns for recall and (string)
  // comparison with a fixed cost of about 25ns. For unordered_map, the empty map costs about 65ns
  // and climbs to about 110ns once there are any entries.
  //
  // The break-even is 4 entries.
  std::map<int64_t, std::unordered_map<std::string, VirtualHostSharedPtr>, std::greater<int64_t>>
      wildcard_virtual_host_suffixes_;
  VirtualHostSharedPtr default_virtual_host_;
};

/**
 * Implementation of Config that reads from a proto file.
 */
class ConfigImpl : public Config {
public:
  ConfigImpl(const envoy::api::v2::RouteConfiguration& config,
             Server::Configuration::FactoryContext& factory_context,
             bool validate_clusters_default);

  const HeaderParser& requestHeaderParser() const { return *request_headers_parser_; };
  const HeaderParser& responseHeaderParser() const { return *response_headers_parser_; };

  // Router::Config
  RouteConstSharedPtr route(const Http::HeaderMap& headers, uint64_t random_value) const override {
    return route_matcher_->route(headers, random_value);
  }

  const std::list<Http::LowerCaseString>& internalOnlyHeaders() const override {
    return internal_only_headers_;
  }

  const std::string& name() const override { return name_; }

private:
  std::unique_ptr<RouteMatcher> route_matcher_;
  std::list<Http::LowerCaseString> internal_only_headers_;
  HeaderParserPtr request_headers_parser_;
  HeaderParserPtr response_headers_parser_;
  const std::string name_;
};

/**
 * Implementation of Config that is empty.
 */
class NullConfigImpl : public Config {
public:
  // Router::Config
  RouteConstSharedPtr route(const Http::HeaderMap&, uint64_t) const override { return nullptr; }

  const std::list<Http::LowerCaseString>& internalOnlyHeaders() const override {
    return internal_only_headers_;
  }

  const std::string& name() const override { return name_; }

private:
  std::list<Http::LowerCaseString> internal_only_headers_;
  const std::string name_;
};

} // namespace Router
} // namespace Envoy
