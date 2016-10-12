#pragma once

#include "envoy/common/optional.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"

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

/**
 * Utility routines for loading route configuration.
 */
class ConfigUtility {
public:
  /**
   * @return the resource priority parsed from JSON.
   */
  static Upstream::ResourcePriority parsePriority(const Json::Object& config);
};

/**
 * Holds all routing configuration for an entire virtual host.
 */
class VirtualHost {
public:
  VirtualHost(const Json::Object& virtual_host, Runtime::Loader& runtime,
              Upstream::ClusterManager& cm);

  const std::string& name() const { return name_; }
  const RedirectEntry* redirectFromEntries(const Http::HeaderMap& headers,
                                           uint64_t random_value) const;
  const RouteEntryImplBase* routeFromEntries(const Http::HeaderMap& headers, bool redirect,
                                             uint64_t random_value) const;
  const VirtualCluster* virtualClusterFromEntries(const Http::HeaderMap& headers) const;

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
  static const SslRedirector SSL_REDIRECTOR;

  const std::string name_;
  std::vector<RouteEntryImplBasePtr> routes_;
  std::vector<VirtualClusterEntry> virtual_clusters_;
  SslRequirements ssl_requirements_;
};

typedef std::shared_ptr<VirtualHost> VirtualHostPtr;

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
 * Implementation of RateLimitPolicy that reads from the JSON route config.
 */
class RateLimitPolicyImpl : public RateLimitPolicy {
public:
  RateLimitPolicyImpl(const Json::Object& config)
      : do_global_limiting_(config.getObject("rate_limit", true).getBoolean("global", false)),
        route_key_(config.getObject("rate_limit", true).getString("route_key", "")) {}

  // Router::RateLimitPolicy
  bool doGlobalLimiting() const override { return do_global_limiting_; }

  // Router::RateLimitPolicy
  const std::string& routeKey() const override { return route_key_; }

private:
  const bool do_global_limiting_;
  const std::string route_key_;
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
class RouteEntryImplBase : public RouteEntry, public Matchable, public RedirectEntry {
public:
  RouteEntryImplBase(const VirtualHost& vhost, const Json::Object& route, Runtime::Loader& loader);

  bool isRedirect() const { return !host_redirect_.empty() || !path_redirect_.empty(); }

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
  const std::string& virtualHostName() const override { return vhost_.name(); }
  std::chrono::milliseconds timeout() const override { return timeout_; }

  // Router::RedirectEntry
  std::string newPath(const Http::HeaderMap& headers) const override;

  // Router::Matchable
  bool matches(const Http::HeaderMap& headers, uint64_t random_value) const override;

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

  struct HeaderData {
    HeaderData(const Http::LowerCaseString& name, const std::string& value)
        : name_(name), value_(value) {}

    const Http::LowerCaseString name_;
    const std::string value_;
  };

  static Optional<RuntimeData> loadRuntimeData(const Json::Object& route);

  // Default timeout is 15s if nothing is specified in the route config.
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  const VirtualHost& vhost_;
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
  std::vector<HeaderData> config_headers_;
};

/**
 * Route entry implementation for prefix path match routing.
 */
class PrefixRouteEntryImpl : public RouteEntryImplBase {
public:
  PrefixRouteEntryImpl(const VirtualHost& vhost, const Json::Object& route,
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
  PathRouteEntryImpl(const VirtualHost& vhost, const Json::Object& route, Runtime::Loader& loader);

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

  const RedirectEntry* redirectRequest(const Http::HeaderMap& headers, uint64_t random_value) const;
  const RouteEntry* routeForRequest(const Http::HeaderMap& headers, uint64_t random_value) const;

private:
  const VirtualHost* findVirtualHost(const Http::HeaderMap& headers) const;

  std::unordered_map<std::string, VirtualHostPtr> virtual_hosts_;
  VirtualHostPtr default_virtual_host_;
};

/**
 * Implementation of Config that reads from a JSON file.
 */
class ConfigImpl : public Config {
public:
  ConfigImpl(const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm);

  // Router::Config
  const RedirectEntry* redirectRequest(const Http::HeaderMap& headers,
                                       uint64_t random_value) const override {
    return route_matcher_->redirectRequest(headers, random_value);
  }

  const RouteEntry* routeForRequest(const Http::HeaderMap& headers,
                                    uint64_t random_value) const override {
    return route_matcher_->routeForRequest(headers, random_value);
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
  const RedirectEntry* redirectRequest(const Http::HeaderMap&, uint64_t) const override {
    return nullptr;
  }

  const RouteEntry* routeForRequest(const Http::HeaderMap&, uint64_t) const override {
    return nullptr;
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

private:
  std::list<Http::LowerCaseString> internal_only_headers_;
  std::list<std::pair<Http::LowerCaseString, std::string>> response_headers_to_add_;
  std::list<Http::LowerCaseString> response_headers_to_remove_;
};

} // Router
