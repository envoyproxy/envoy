#pragma once

#include "router_ratelimit.h"

#include "envoy/common/optional.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/json/json_loader.h"

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
  virtual bool matches(const Http::HeaderMap& headers, uint64_t random_value) const PURE;
};

class RouteEntryImplBase;
typedef std::shared_ptr<RouteEntryImplBase> RouteEntryImplBasePtr;

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

/**
 * Utility routines for loading route configuration and matching runtime request headers.
 */
class ConfigUtility {
public:
  struct HeaderData {
    HeaderData(const Http::LowerCaseString& name, const std::string& value, const bool is_regex)
        : name_(name), value_(value), regex_pattern_(value_, std::regex::optimize),
          is_regex_(is_regex) {}

    const Http::LowerCaseString name_;
    const std::string value_;
    const std::regex regex_pattern_;
    const bool is_regex_;
  };

  /**
   * @return the resource priority parsed from JSON.
   */
  static Upstream::ResourcePriority parsePriority(const Json::Object& config);

  /**
   * See if the specified headers are present in the request headers.
   * @param headers supplies the list of headers to match
   * @param request_headers supplies the list of request headers to compare against search_list
   * @return true all the headers (and values) in the search_list set are found in the
   * request_headers
   */
  static bool matchHeaders(const Http::HeaderMap& headers,
                           const std::vector<HeaderData> request_headers);
};

/**
 * Holds all routing configuration for an entire virtual host.
 */
class VirtualHostImpl : public VirtualHost {
public:
  VirtualHostImpl(const Json::Object& virtual_host, Runtime::Loader& runtime,
                  Upstream::ClusterManager& cm);

  const Route* getRouteFromEntries(const Http::HeaderMap& headers, uint64_t random_value) const;
  bool usesRuntime() const;
  const VirtualCluster* virtualClusterFromEntries(const Http::HeaderMap& headers) const;

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
  static const SslRedirectRoute SSL_REDIRECT_ROUTE;

  const std::string name_;
  std::vector<RouteEntryImplBasePtr> routes_;
  std::vector<VirtualClusterEntry> virtual_clusters_;
  SslRequirements ssl_requirements_;
  const RateLimitPolicyImpl rate_limit_policy_;
};

typedef std::shared_ptr<VirtualHostImpl> VirtualHostPtr;

/**
 * Implementation of RetryPolicy that reads from the JSON route config.
 */
class RetryPolicyImpl : public RetryPolicy {
public:
  RetryPolicyImpl(const Json::Object& config);

  // Router::RetryPolicy
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }

private:
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
 * Base implementation for all route entries.
 */
class RouteEntryImplBase : public RouteEntry, public Matchable, public RedirectEntry, public Route {
public:
  RouteEntryImplBase(const VirtualHostImpl& vhost, const Json::Object& route,
                     Runtime::Loader& loader);

  bool isRedirect() const { return !host_redirect_.empty() || !path_redirect_.empty(); }
  bool usesRuntime() const { return runtime_.valid(); }

  // Router::RouteEntry
  const std::string& clusterName() const override;
  void finalizeRequestHeaders(Http::HeaderMap& headers) const override;
  Upstream::ResourcePriority priority() const override { return priority_; }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  const RetryPolicy& retryPolicy() const override { return retry_policy_; }
  const ShadowPolicy& shadowPolicy() const override { return shadow_policy_; }
  const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
    return vhost_.virtualClusterFromEntries(headers);
  }
  std::chrono::milliseconds timeout() const override { return timeout_; }
  const VirtualHost& virtualHost() const override { return vhost_; }

  void validateClusters(Upstream::ClusterManager& cm) const;
  bool isWeightedCluster() const { return !weighted_clusters_.empty(); }
  const Route* weightedClusterEntry(uint64_t random_value) const;

  // Router::RedirectEntry
  std::string newPath(const Http::HeaderMap& headers) const override;

  // Router::Matchable
  bool matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

  // Router::Route
  const RedirectEntry* redirectEntry() const override;
  const RouteEntry* routeEntry() const override;

protected:
  const bool case_sensitive_;
  const std::string prefix_rewrite_;
  const std::string host_rewrite_;

  void finalizePathHeader(Http::HeaderMap& headers, const std::string& matched_path) const;

private:
  struct RuntimeData {
    std::string key_;
    uint64_t default_;
  };

  /**
   * Route entry implementation for weighted clusters.
   * RouteEntryImplBase holds one or more weighted cluster objects,
   * where each object has a back pointer to the parent
   * RouteEntryImplBase object. Almost all functions in this class
   * forward calls back to the parent, with the exception of
   * clusterName and routeEntry.
   */
  struct WeightedClusterEntry : public RouteEntry, public Route {
  public:
    WeightedClusterEntry(const RouteEntryImplBase* parent, const std::string runtime_key,
                         Runtime::Loader& loader, const std::string name, uint64_t weight)
        : parent_(parent), runtime_key_(runtime_key), loader_(loader), cluster_name_(name),
          cluster_weight_(weight) {}

    uint64_t clusterWeight() const {
      return loader_.snapshot().getInteger(runtime_key_, cluster_weight_);
    }

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }

    void finalizeRequestHeaders(Http::HeaderMap& headers) const override {
      return parent_->finalizeRequestHeaders(headers);
    }

    Upstream::ResourcePriority priority() const override { return parent_->priority(); }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_->rateLimitPolicy(); }
    const RetryPolicy& retryPolicy() const override { return parent_->retryPolicy(); }
    const ShadowPolicy& shadowPolicy() const override { return parent_->shadowPolicy(); }
    std::chrono::milliseconds timeout() const override { return parent_->timeout(); }

    const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override {
      return parent_->virtualCluster(headers);
    }

    const VirtualHost& virtualHost() const override { return parent_->virtualHost(); }

    // Router::Route
    const RedirectEntry* redirectEntry() const override { return nullptr; }
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase* parent_;
    const std::string runtime_key_;
    Runtime::Loader& loader_;
    const std::string cluster_name_;
    uint64_t cluster_weight_;
  };
  typedef std::shared_ptr<WeightedClusterEntry> WeightedClusterEntryPtr;

  static Optional<RuntimeData> loadRuntimeData(const Json::Object& route);

  // Default timeout is 15s if nothing is specified in the route config.
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  const VirtualHostImpl& vhost_;
  const std::string cluster_name_;
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
  std::vector<WeightedClusterEntryPtr> weighted_clusters_;
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
  bool matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

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
  bool matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

private:
  const std::string path_;
};

/**
 * Wraps the route configuration which matches an incoming request headers to a backend cluster.
 * This is split out mainly to help with unit testing.
 */
class RouteMatcher {
public:
  RouteMatcher(const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm);

  const Route* route(const Http::HeaderMap& headers, uint64_t random_value) const;
  bool usesRuntime() const { return uses_runtime_; }

private:
  const VirtualHostImpl* findVirtualHost(const Http::HeaderMap& headers) const;

  std::unordered_map<std::string, VirtualHostPtr> virtual_hosts_;
  VirtualHostPtr default_virtual_host_;
  bool uses_runtime_{};
};

/**
 * Implementation of Config that reads from a JSON file.
 */
class ConfigImpl : public Config {
public:
  ConfigImpl(const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm);

  // Router::Config
  const Route* route(const Http::HeaderMap& headers, uint64_t random_value) const override {
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
};

/**
 * Implementation of Config that is empty.
 */
class NullConfigImpl : public Config {
public:
  // Router::Config
  const Route* route(const Http::HeaderMap&, uint64_t) const override { return nullptr; }

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
