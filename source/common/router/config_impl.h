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

#include "envoy/common/optional.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/router/config_utility.h"
#include "common/router/router_ratelimit.h"

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

class RouteEntryImplBase;
typedef std::shared_ptr<const RouteEntryImplBase> RouteEntryImplBaseConstSharedPtr;

/**
 * Redirect entry that does an SSL redirect.
 */
class SslRedirector : public RedirectEntry {
public:
  // Router::RedirectEntry
  std::string newPath(const Http::HeaderMap& headers) const override;
};

class SslRedirectRoute : public Route {
public:
  // Router::Route
  const RedirectEntry* redirectEntry() const override { return &SSL_REDIRECTOR; }
  const RouteEntry* routeEntry() const override { return nullptr; }

private:
  static const SslRedirector SSL_REDIRECTOR;
};

class ConfigImpl;
/**
 * Holds all routing configuration for an entire virtual host.
 */
class VirtualHostImpl : public VirtualHost {
public:
  VirtualHostImpl(const Json::Object& virtual_host, const ConfigImpl& global_route_config,
                  Runtime::Loader& runtime, Upstream::ClusterManager& cm, bool validate_clusters);

  RouteConstSharedPtr getRouteFromEntries(const Http::HeaderMap& headers,
                                          uint64_t random_value) const;
  bool usesRuntime() const;
  const VirtualCluster* virtualClusterFromEntries(const Http::HeaderMap& headers) const;
  const std::list<std::pair<Http::LowerCaseString, std::string>>& requestHeadersToAdd() const {
    return request_headers_to_add_;
  }
  const ConfigImpl& globalRouteConfig() const { return global_route_config_; }

  // Router::VirtualHost
  const std::string& name() const override { return name_; }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }

private:
  enum class SslRequirements { NONE, EXTERNAL_ONLY, ALL };

  struct VirtualClusterEntry : public VirtualCluster {
    VirtualClusterEntry(const Json::Object& virtual_cluster);

    // Router::VirtualCluster
    const std::string& name() const override { return name_; }
    Upstream::ResourcePriority priority() const override { return priority_; }

    std::regex pattern_;
    Optional<std::string> method_;
    std::string name_;
    Upstream::ResourcePriority priority_;
  };

  struct CatchAllVirtualCluster : public VirtualCluster {
    // Router::VirtualCluster
    const std::string& name() const override { return name_; }
    Upstream::ResourcePriority priority() const override {
      return Upstream::ResourcePriority::Default;
    }

    std::string name_{"other"};
  };

  static const CatchAllVirtualCluster VIRTUAL_CLUSTER_CATCH_ALL;
  static const std::shared_ptr<const SslRedirectRoute> SSL_REDIRECT_ROUTE;

  const std::string name_;
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
  std::vector<VirtualClusterEntry> virtual_clusters_;
  SslRequirements ssl_requirements_;
  const RateLimitPolicyImpl rate_limit_policy_;
  const ConfigImpl& global_route_config_;
  std::list<std::pair<Http::LowerCaseString, std::string>> request_headers_to_add_;
};

typedef std::shared_ptr<VirtualHostImpl> VirtualHostSharedPtr;

/**
 * Implementation of RetryPolicy that reads from the JSON route config.
 */
class RetryPolicyImpl : public RetryPolicy {
public:
  RetryPolicyImpl(const Json::Object& config);

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
 * Implementation of ShadowPolicy that reads from the JSON route config.
 */
class ShadowPolicyImpl : public ShadowPolicy {
public:
  ShadowPolicyImpl(const Json::Object& config);

  // Router::ShadowPolicy
  const std::string& cluster() const override { return cluster_; }
  const std::string& runtimeKey() const override { return runtime_key_; }

private:
  std::string cluster_;
  std::string runtime_key_;
};

/**
 * Implementation of HashPolicy that reads from the JSON route config and only currently supports
 * hashing on an HTTP header.
 */
class HashPolicyImpl : public HashPolicy {
public:
  HashPolicyImpl(const Json::Object& config);

  // Router::HashPolicy
  Optional<uint64_t> generateHash(const Http::HeaderMap& headers) const override;

private:
  const Http::LowerCaseString header_name_;
};

/**
 * Base implementation for all route entries.
 */
class RouteEntryImplBase : public RouteEntry,
                           public Matchable,
                           public RedirectEntry,
                           public Route,
                           public std::enable_shared_from_this<RouteEntryImplBase> {
public:
  RouteEntryImplBase(const VirtualHostImpl& vhost, const Json::Object& route,
                     Runtime::Loader& loader);

  bool isRedirect() const { return !host_redirect_.empty() || !path_redirect_.empty(); }
  bool usesRuntime() const { return runtime_.valid(); }

  bool matchRoute(const Http::HeaderMap& headers, uint64_t random_value) const;
  void validateClusters(Upstream::ClusterManager& cm) const;
  const std::list<std::pair<Http::LowerCaseString, std::string>>& requestHeadersToAdd() const {
    return request_headers_to_add_;
  }

  // Router::RouteEntry
  const std::string& clusterName() const override;
  void finalizeRequestHeaders(Http::HeaderMap& headers) const override;
  const HashPolicy* hashPolicy() const override { return hash_policy_.get(); }
  Upstream::ResourcePriority priority() const override { return priority_; }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const RetryPolicy& retryPolicy() const override { return retry_policy_; }
  const ShadowPolicy& shadowPolicy() const override { return shadow_policy_; }
  const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
    return vhost_.virtualClusterFromEntries(headers);
  }
  std::chrono::milliseconds timeout() const override { return timeout_; }
  const VirtualHost& virtualHost() const override { return vhost_; }
  bool autoHostRewrite() const override { return auto_host_rewrite_; }
  const std::multimap<std::string, std::string>& opaqueConfig() const override {
    return opaque_config_;
  }
  bool includeVirtualHostRateLimits() const override { return include_vh_rate_limits_; }

  // Router::RedirectEntry
  std::string newPath(const Http::HeaderMap& headers) const override;

  // Router::Route
  const RedirectEntry* redirectEntry() const override;
  const RouteEntry* routeEntry() const override;

protected:
  const bool case_sensitive_;
  const std::string prefix_rewrite_;
  const std::string host_rewrite_;
  bool include_vh_rate_limits_;

  RouteConstSharedPtr clusterEntry(const Http::HeaderMap& headers, uint64_t random_value) const;
  void finalizePathHeader(Http::HeaderMap& headers, const std::string& matched_path) const;

private:
  struct RuntimeData {
    std::string key_;
    uint64_t default_;
  };

  class DynanmicRouteEntry : public RouteEntry, public Route {
  public:
    DynanmicRouteEntry(const RouteEntryImplBase* parent, const std::string& name)
        : parent_(parent), cluster_name_(name) {}

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }

    void finalizeRequestHeaders(Http::HeaderMap& headers) const override {
      return parent_->finalizeRequestHeaders(headers);
    }

    const HashPolicy* hashPolicy() const override { return parent_->hashPolicy(); }
    Upstream::ResourcePriority priority() const override { return parent_->priority(); }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_->rateLimitPolicy(); }
    const RetryPolicy& retryPolicy() const override { return parent_->retryPolicy(); }
    const ShadowPolicy& shadowPolicy() const override { return parent_->shadowPolicy(); }
    std::chrono::milliseconds timeout() const override { return parent_->timeout(); }

    const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
      return parent_->virtualCluster(headers);
    }

    const std::multimap<std::string, std::string>& opaqueConfig() const override {
      return parent_->opaqueConfig();
    }

    const VirtualHost& virtualHost() const override { return parent_->virtualHost(); }
    bool autoHostRewrite() const override { return parent_->autoHostRewrite(); }
    bool includeVirtualHostRateLimits() const override {
      return parent_->includeVirtualHostRateLimits();
    }

    // Router::Route
    const RedirectEntry* redirectEntry() const override { return nullptr; }
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase* parent_;
    const std::string cluster_name_;
  };

  /**
   * Route entry implementation for weighted clusters. The RouteEntryImplBase object holds
   * one or more weighted cluster objects, where each object has a back pointer to the parent
   * RouteEntryImplBase object. Almost all functions in this class forward calls back to the
   * parent, with the exception of clusterName and routeEntry.
   */
  class WeightedClusterEntry : public DynanmicRouteEntry {
  public:
    WeightedClusterEntry(const RouteEntryImplBase* parent, const std::string runtime_key,
                         Runtime::Loader& loader, const std::string& name, uint64_t weight)
        : DynanmicRouteEntry(parent, name), runtime_key_(runtime_key), loader_(loader),
          cluster_weight_(weight) {}

    uint64_t clusterWeight() const {
      return loader_.snapshot().getInteger(runtime_key_, cluster_weight_);
    }

    static const uint64_t MAX_CLUSTER_WEIGHT;

  private:
    const std::string runtime_key_;
    Runtime::Loader& loader_;
    const uint64_t cluster_weight_;
  };

  typedef std::shared_ptr<WeightedClusterEntry> WeightedClusterEntrySharedPtr;

  static Optional<RuntimeData> loadRuntimeData(const Json::Object& route);

  static std::multimap<std::string, std::string> parseOpaqueConfig(const Json::Object& route);

  // Default timeout is 15s if nothing is specified in the route config.
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  const VirtualHostImpl& vhost_;
  const bool auto_host_rewrite_;
  const std::string cluster_name_;
  const Http::LowerCaseString cluster_header_name_;
  const std::chrono::milliseconds timeout_;
  const Optional<RuntimeData> runtime_;
  Runtime::Loader& loader_;
  const std::string host_redirect_;
  const std::string path_redirect_;
  const RetryPolicyImpl retry_policy_;
  const RateLimitPolicyImpl rate_limit_policy_;
  const ShadowPolicyImpl shadow_policy_;
  const Upstream::ResourcePriority priority_;
  std::vector<ConfigUtility::HeaderData> config_headers_;
  std::vector<WeightedClusterEntrySharedPtr> weighted_clusters_;
  std::unique_ptr<const HashPolicyImpl> hash_policy_;
  std::list<std::pair<Http::LowerCaseString, std::string>> request_headers_to_add_;

  // TODO(danielhochman): refactor multimap into unordered_map since JSON is unordered map.
  const std::multimap<std::string, std::string> opaque_config_;
};

/**
 * Route entry implementation for prefix path match routing.
 */
class PrefixRouteEntryImpl : public RouteEntryImplBase {
public:
  PrefixRouteEntryImpl(const VirtualHostImpl& vhost, const Json::Object& route,
                       Runtime::Loader& loader);

  // Router::RouteEntry
  void finalizeRequestHeaders(Http::HeaderMap& headers) const override;

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

private:
  const std::string prefix_;
};

/**
 * Route entry implementation for exact path match routing.
 */
class PathRouteEntryImpl : public RouteEntryImplBase {
public:
  PathRouteEntryImpl(const VirtualHostImpl& vhost, const Json::Object& route,
                     Runtime::Loader& loader);

  // Router::RouteEntry
  void finalizeRequestHeaders(Http::HeaderMap& headers) const override;

  // Router::Matchable
  RouteConstSharedPtr matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

private:
  const std::string path_;
};

/**
 * Wraps the route configuration which matches an incoming request headers to a backend cluster.
 * This is split out mainly to help with unit testing.
 */
class RouteMatcher {
public:
  RouteMatcher(const Json::Object& config, const ConfigImpl& global_http_config,
               Runtime::Loader& runtime, Upstream::ClusterManager& cm, bool validate_clusters);

  RouteConstSharedPtr route(const Http::HeaderMap& headers, uint64_t random_value) const;
  bool usesRuntime() const { return uses_runtime_; }

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
  bool uses_runtime_{};
};

/**
 * Implementation of Config that reads from a JSON file.
 */
class ConfigImpl : public Config {
public:
  ConfigImpl(const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm,
             bool validate_clusters);

  const std::list<std::pair<Http::LowerCaseString, std::string>>& requestHeadersToAdd() const {
    return request_headers_to_add_;
  }

  // Router::Config
  RouteConstSharedPtr route(const Http::HeaderMap& headers, uint64_t random_value) const override {
    return route_matcher_->route(headers, random_value);
  }

  const std::list<Http::LowerCaseString>& internalOnlyHeaders() const override {
    return internal_only_headers_;
  }

  const std::list<std::pair<Http::LowerCaseString, std::string>>&
  responseHeadersToAdd() const override {
    return response_headers_to_add_;
  }

  const std::list<Http::LowerCaseString>& responseHeadersToRemove() const override {
    return response_headers_to_remove_;
  }

  bool usesRuntime() const override { return route_matcher_->usesRuntime(); }

private:
  std::unique_ptr<RouteMatcher> route_matcher_;
  std::list<Http::LowerCaseString> internal_only_headers_;
  std::list<std::pair<Http::LowerCaseString, std::string>> response_headers_to_add_;
  std::list<Http::LowerCaseString> response_headers_to_remove_;
  std::list<std::pair<Http::LowerCaseString, std::string>> request_headers_to_add_;
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

  const std::list<std::pair<Http::LowerCaseString, std::string>>&
  responseHeadersToAdd() const override {
    return response_headers_to_add_;
  }

  const std::list<Http::LowerCaseString>& responseHeadersToRemove() const override {
    return response_headers_to_remove_;
  }

  bool usesRuntime() const override { return false; }

private:
  std::list<Http::LowerCaseString> internal_only_headers_;
  std::list<std::pair<Http::LowerCaseString, std::string>> response_headers_to_add_;
  std::list<Http::LowerCaseString> response_headers_to_remove_;
};

} // Router
} // Envoy
