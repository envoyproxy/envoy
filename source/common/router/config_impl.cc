#include "common/router/config_impl.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/router/retry_state_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Router {

std::string SslRedirector::newPath(const Http::HeaderMap& headers) const {
  return Http::Utility::createSslRedirectPath(headers);
}

RetryPolicyImpl::RetryPolicyImpl(const Json::Object& config) {
  if (!config.hasObject("retry_policy")) {
    return;
  }

  per_try_timeout_ = std::chrono::milliseconds(
      config.getObject("retry_policy")->getInteger("per_try_timeout_ms", 0));
  num_retries_ = config.getObject("retry_policy")->getInteger("num_retries", 1);
  retry_on_ = RetryStateImpl::parseRetryOn(config.getObject("retry_policy")->getString("retry_on"));
  retry_on_ |=
      RetryStateImpl::parseRetryGrpcOn(config.getObject("retry_policy")->getString("retry_on"));
}

ShadowPolicyImpl::ShadowPolicyImpl(const Json::Object& config) {
  if (!config.hasObject("shadow")) {
    return;
  }

  cluster_ = config.getObject("shadow")->getString("cluster");
  runtime_key_ = config.getObject("shadow")->getString("runtime_key", "");
}

HashPolicyImpl::HashPolicyImpl(const Json::Object& config)
    : header_name_(config.getString("header_name")) {}

Optional<uint64_t> HashPolicyImpl::generateHash(const Http::HeaderMap& headers) const {
  Optional<uint64_t> hash;
  const Http::HeaderEntry* header = headers.get(header_name_);
  if (header) {
    // TODO(mattklein123): Compile in murmur3/city/etc. and potentially allow the user to choose so
    // we know exactly what we are going to get.
    hash.value(std::hash<std::string>()(header->value().c_str()));
  }
  return hash;
}

const uint64_t RouteEntryImplBase::WeightedClusterEntry::MAX_CLUSTER_WEIGHT = 100UL;

RouteEntryImplBase::RouteEntryImplBase(const VirtualHostImpl& vhost, const Json::Object& route,
                                       Runtime::Loader& loader)
    : case_sensitive_(route.getBoolean("case_sensitive", true)),
      prefix_rewrite_(route.getString("prefix_rewrite", "")),
      host_rewrite_(route.getString("host_rewrite", "")), vhost_(vhost),
      auto_host_rewrite_(route.getBoolean("auto_host_rewrite", false)),
      cluster_name_(route.getString("cluster", "")),
      cluster_header_name_(route.getString("cluster_header", "")),
      timeout_(route.getInteger("timeout_ms", DEFAULT_ROUTE_TIMEOUT_MS)),
      runtime_(loadRuntimeData(route)), loader_(loader),
      host_redirect_(route.getString("host_redirect", "")),
      path_redirect_(route.getString("path_redirect", "")), retry_policy_(route),
      rate_limit_policy_(route), shadow_policy_(route),
      priority_(ConfigUtility::parsePriority(route)), opaque_config_(parseOpaqueConfig(route)) {

  route.validateSchema(Json::Schema::ROUTE_ENTRY_CONFIGURATION_SCHEMA);

  // Route can either have a host_rewrite with fixed host header or automatic host rewrite
  // based on the DNS name of the instance in the backing cluster.
  if (auto_host_rewrite_ && !host_rewrite_.empty()) {
    throw EnvoyException("routes cannot have both auto_host_rewrite and host_rewrite options set");
  }

  bool have_weighted_clusters = route.hasObject("weighted_clusters");
  bool have_cluster =
      !cluster_name_.empty() || !cluster_header_name_.get().empty() || have_weighted_clusters;

  // Check to make sure that we are either a redirect route or we have a cluster.
  if (!(isRedirect() ^ have_cluster)) {
    throw EnvoyException("routes must be either redirects or cluster targets");
  }

  if (have_cluster) {
    // This is a trick to do a three-way XOR. It would be nice if we could do this with the JSON
    // schema but there is no obvious way to do this.
    if ((!cluster_name_.empty() + !cluster_header_name_.get().empty() + have_weighted_clusters) !=
        1) {
      throw EnvoyException("routes must specify one of cluster/cluster_header/weighted_clusters");
    }
  }

  // If this is a weighted_cluster, we create N internal route entries
  // (called WeightedClusterEntry), such that each object is a simple
  // single cluster, pointing back to the parent.
  if (have_weighted_clusters) {
    uint64_t total_weight = 0UL;

    const Json::ObjectSharedPtr weighted_clusters_json = route.getObject("weighted_clusters");
    const std::vector<Json::ObjectSharedPtr> cluster_list =
        weighted_clusters_json->getObjectArray("clusters");
    const std::string runtime_key_prefix =
        weighted_clusters_json->getString("runtime_key_prefix", EMPTY_STRING);

    for (const Json::ObjectSharedPtr& cluster : cluster_list) {
      const std::string cluster_name = cluster->getString("name");
      std::unique_ptr<WeightedClusterEntry> cluster_entry(
          new WeightedClusterEntry(this, runtime_key_prefix + "." + cluster_name, loader_,
                                   cluster_name, cluster->getInteger("weight")));
      weighted_clusters_.emplace_back(std::move(cluster_entry));
      total_weight += weighted_clusters_.back()->clusterWeight();
    }

    if (total_weight != WeightedClusterEntry::MAX_CLUSTER_WEIGHT) {
      throw EnvoyException(fmt::format("Sum of weights in the weighted_cluster should add up to {}",
                                       WeightedClusterEntry::MAX_CLUSTER_WEIGHT));
    }
  }

  if (route.hasObject("headers")) {
    std::vector<Json::ObjectSharedPtr> config_headers = route.getObjectArray("headers");
    for (const Json::ObjectSharedPtr& header_map : config_headers) {
      config_headers_.push_back(*header_map);
    }
  }

  if (route.hasObject("hash_policy")) {
    hash_policy_.reset(new HashPolicyImpl(*route.getObject("hash_policy")));
  }

  if (route.hasObject("request_headers_to_add")) {
    for (const Json::ObjectSharedPtr& header : route.getObjectArray("request_headers_to_add")) {
      request_headers_to_add_.push_back(
          {Http::LowerCaseString(header->getString("key")), header->getString("value")});
    }
  }

  // Only set include_vh_rate_limits_ to true if the rate limit policy for the route is empty
  // or the route set `include_vh_rate_limits` to true.
  include_vh_rate_limits_ =
      (rate_limit_policy_.empty() || route.getBoolean("include_vh_rate_limits", false));
}

bool RouteEntryImplBase::matchRoute(const Http::HeaderMap& headers, uint64_t random_value) const {
  bool matches = true;

  if (runtime_.valid()) {
    matches &= loader_.snapshot().featureEnabled(runtime_.value().key_, runtime_.value().default_,
                                                 random_value);
  }

  matches &= ConfigUtility::matchHeaders(headers, config_headers_);

  return matches;
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

void RouteEntryImplBase::finalizeRequestHeaders(Http::HeaderMap& headers) const {
  // Append user-specified request headers in the following order: route-level headers,
  // virtual host level headers and finally global connection manager level headers.
  for (const std::pair<Http::LowerCaseString, std::string>& to_add : requestHeadersToAdd()) {
    headers.addStatic(to_add.first, to_add.second);
  }
  for (const std::pair<Http::LowerCaseString, std::string>& to_add : vhost_.requestHeadersToAdd()) {
    headers.addStatic(to_add.first, to_add.second);
  }
  for (const std::pair<Http::LowerCaseString, std::string>& to_add :
       vhost_.globalRouteConfig().requestHeadersToAdd()) {
    headers.addStatic(to_add.first, to_add.second);
  }

  if (host_rewrite_.empty()) {
    return;
  }

  headers.Host()->value(host_rewrite_);
}

Optional<RouteEntryImplBase::RuntimeData>
RouteEntryImplBase::loadRuntimeData(const Json::Object& route) {
  Optional<RuntimeData> runtime;
  if (route.hasObject("runtime")) {
    RuntimeData data;
    data.key_ = route.getObject("runtime")->getString("key");
    data.default_ = route.getObject("runtime")->getInteger("default");
    runtime.value(data);
  }

  return runtime;
}

void RouteEntryImplBase::finalizePathHeader(Http::HeaderMap& headers,
                                            const std::string& matched_path) const {
  if (prefix_rewrite_.empty()) {
    return;
  }

  std::string path = headers.Path()->value().c_str();
  headers.insertEnvoyOriginalPath().value(*headers.Path());
  ASSERT(StringUtil::startsWith(path.c_str(), matched_path, case_sensitive_));
  headers.Path()->value(path.replace(0, matched_path.size(), prefix_rewrite_));
}

std::string RouteEntryImplBase::newPath(const Http::HeaderMap& headers) const {
  ASSERT(isRedirect());

  const char* final_host;
  const char* final_path;
  if (!host_redirect_.empty()) {
    final_host = host_redirect_.c_str();
  } else {
    ASSERT(headers.Host());
    final_host = headers.Host()->value().c_str();
  }

  if (!path_redirect_.empty()) {
    final_path = path_redirect_.c_str();
  } else {
    ASSERT(headers.Path());
    final_path = headers.Path()->value().c_str();
  }

  ASSERT(headers.ForwardedProto());
  return fmt::format("{}://{}{}", headers.ForwardedProto()->value().c_str(), final_host,
                     final_path);
}

std::multimap<std::string, std::string>
RouteEntryImplBase::parseOpaqueConfig(const Json::Object& route) {
  std::multimap<std::string, std::string> ret;
  if (route.hasObject("opaque_config")) {
    Json::ObjectSharedPtr obj = route.getObject("opaque_config");
    obj->iterate([&ret](const std::string& name, const Json::Object& value) {
      ret.emplace(name, value.asString());
      return true;
    });
  }
  return ret;
}

const RedirectEntry* RouteEntryImplBase::redirectEntry() const {
  // A route for a request can exclusively be a route entry or a redirect entry.
  if (isRedirect()) {
    return this;
  } else {
    return nullptr;
  }
}

const RouteEntry* RouteEntryImplBase::routeEntry() const {
  // A route for a request can exclusively be a route entry or a redirect entry.
  if (isRedirect()) {
    return nullptr;
  } else {
    return this;
  }
}

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(const Http::HeaderMap& headers,
                                                     uint64_t random_value) const {
  // Gets the route object chosen from the list of weighted clusters
  // (if there is one) or returns self.
  if (weighted_clusters_.empty()) {
    if (!cluster_name_.empty() || isRedirect()) {
      return shared_from_this();
    } else {
      ASSERT(!cluster_header_name_.get().empty());
      const Http::HeaderEntry* entry = headers.get(cluster_header_name_);
      std::string final_cluster_name;
      if (entry) {
        final_cluster_name = entry->value().c_str();
      }

      // NOTE: Though we return a shared_ptr here, the current ownership model assumes that
      //       the route table sticks around. In v1 of RDS we will likely snap the route table
      //       in the connection manager to account for this, but we may eventually want to have
      //       a chain of references that goes back to the route table root. That is complicated
      //       though.
      return std::make_shared<DynamicRouteEntry>(this, final_cluster_name);
    }
  }

  uint64_t selected_value = random_value % WeightedClusterEntry::MAX_CLUSTER_WEIGHT;
  uint64_t begin = 0UL;
  uint64_t end = 0UL;

  // Find the right cluster to route to based on the interval in which
  // the selected value falls.  The intervals are determined as
  // [0, cluster1_weight), [cluster1_weight, cluster1_weight+cluster2_weight),..
  for (const WeightedClusterEntrySharedPtr& cluster : weighted_clusters_) {
    end = begin + cluster->clusterWeight();
    if (((selected_value >= begin) && (selected_value < end)) ||
        (end >= WeightedClusterEntry::MAX_CLUSTER_WEIGHT)) {
      // end > WeightedClusterEntry::MAX_CLUSTER_WEIGHT : This case can only occur
      // with Runtimes, when the user specifies invalid weights such that
      // sum(weights) > WeightedClusterEntry::MAX_CLUSTER_WEIGHT.
      // In this case, terminate the search and just return the cluster
      // whose weight cased the overflow
      return cluster;
    }
    begin = end;
  }
  NOT_REACHED;
}

void RouteEntryImplBase::validateClusters(Upstream::ClusterManager& cm) const {
  if (isRedirect()) {
    return;
  }

  // Currently, we verify that the cluster exists in the CM if we have an explicit cluster or
  // weighted cluster rule. We obviously do not verify a cluster_header rule. This means that
  // trying to use all CDS clusters with a static route table will not work. In the upcoming RDS
  // change we will make it so that dynamically loaded route tables do *not* perform CM checks.
  // In the future we might decide to also have a config option that turns off checks for static
  // route tables. This would enable the all CDS with static route table case.
  if (!cluster_name_.empty()) {
    if (!cm.get(cluster_name_)) {
      throw EnvoyException(fmt::format("route: unknown cluster '{}'", cluster_name_));
    }
  } else if (!weighted_clusters_.empty()) {
    for (const WeightedClusterEntrySharedPtr& cluster : weighted_clusters_) {
      if (!cm.get(cluster->clusterName())) {
        throw EnvoyException(
            fmt::format("route: unknown weighted cluster '{}'", cluster->clusterName()));
      }
    }
  }
}

PrefixRouteEntryImpl::PrefixRouteEntryImpl(const VirtualHostImpl& vhost, const Json::Object& route,
                                           Runtime::Loader& loader)
    : RouteEntryImplBase(vhost, route, loader), prefix_(route.getString("prefix")) {}

void PrefixRouteEntryImpl::finalizeRequestHeaders(Http::HeaderMap& headers) const {
  RouteEntryImplBase::finalizeRequestHeaders(headers);

  finalizePathHeader(headers, prefix_);
}

RouteConstSharedPtr PrefixRouteEntryImpl::matches(const Http::HeaderMap& headers,
                                                  uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, random_value) &&
      StringUtil::startsWith(headers.Path()->value().c_str(), prefix_, case_sensitive_)) {
    return clusterEntry(headers, random_value);
  }
  return nullptr;
}

PathRouteEntryImpl::PathRouteEntryImpl(const VirtualHostImpl& vhost, const Json::Object& route,
                                       Runtime::Loader& loader)
    : RouteEntryImplBase(vhost, route, loader), path_(route.getString("path")) {}

void PathRouteEntryImpl::finalizeRequestHeaders(Http::HeaderMap& headers) const {
  RouteEntryImplBase::finalizeRequestHeaders(headers);

  finalizePathHeader(headers, path_);
}

RouteConstSharedPtr PathRouteEntryImpl::matches(const Http::HeaderMap& headers,
                                                uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, random_value)) {
    // TODO(mattklein123) PERF: Avoid copy.
    std::string path = headers.Path()->value().c_str();
    size_t query_string_start = path.find("?");

    if (case_sensitive_) {
      if (path.substr(0, query_string_start) == path_) {
        return clusterEntry(headers, random_value);
      }
    } else {
      if (StringUtil::caseInsensitiveCompare(path.substr(0, query_string_start).c_str(),
                                             path_.c_str()) == 0) {
        return clusterEntry(headers, random_value);
      }
    }
  }

  return nullptr;
}

VirtualHostImpl::VirtualHostImpl(const Json::Object& virtual_host,
                                 const ConfigImpl& global_route_config, Runtime::Loader& runtime,
                                 Upstream::ClusterManager& cm, bool validate_clusters)
    : name_(virtual_host.getString("name")), rate_limit_policy_(virtual_host),
      global_route_config_(global_route_config) {

  virtual_host.validateSchema(Json::Schema::VIRTUAL_HOST_CONFIGURATION_SCHEMA);

  std::string require_ssl = virtual_host.getString("require_ssl", "");
  if (require_ssl == "") {
    ssl_requirements_ = SslRequirements::NONE;
  } else if (require_ssl == "all") {
    ssl_requirements_ = SslRequirements::ALL;
  } else {
    ASSERT(require_ssl == "external_only");
    ssl_requirements_ = SslRequirements::EXTERNAL_ONLY;
  }

  if (virtual_host.hasObject("request_headers_to_add")) {
    for (const Json::ObjectSharedPtr& header :
         virtual_host.getObjectArray("request_headers_to_add")) {
      request_headers_to_add_.push_back(
          {Http::LowerCaseString(header->getString("key")), header->getString("value")});
    }
  }

  for (const Json::ObjectSharedPtr& route : virtual_host.getObjectArray("routes")) {
    bool has_prefix = route->hasObject("prefix");
    bool has_path = route->hasObject("path");
    if (!(has_prefix ^ has_path)) {
      throw EnvoyException("routes must specify either prefix or path");
    }

    if (has_prefix) {
      routes_.emplace_back(new PrefixRouteEntryImpl(*this, *route, runtime));
    } else {
      ASSERT(has_path);
      routes_.emplace_back(new PathRouteEntryImpl(*this, *route, runtime));
    }

    if (validate_clusters) {
      routes_.back()->validateClusters(cm);
      if (!routes_.back()->shadowPolicy().cluster().empty()) {
        if (!cm.get(routes_.back()->shadowPolicy().cluster())) {
          throw EnvoyException(fmt::format("route: unknown shadow cluster '{}'",
                                           routes_.back()->shadowPolicy().cluster()));
        }
      }
    }
  }

  if (virtual_host.hasObject("virtual_clusters")) {
    for (const Json::ObjectSharedPtr& virtual_cluster :
         virtual_host.getObjectArray("virtual_clusters")) {
      virtual_clusters_.push_back(VirtualClusterEntry(*virtual_cluster));
    }
  }
}

bool VirtualHostImpl::usesRuntime() const {
  bool uses = false;
  for (const RouteEntryImplBaseConstSharedPtr& route : routes_) {
    // Currently a base runtime rule as well as a shadow rule can use runtime.
    uses |= (route->usesRuntime() || !route->shadowPolicy().runtimeKey().empty());
  }

  return uses;
}

VirtualHostImpl::VirtualClusterEntry::VirtualClusterEntry(const Json::Object& virtual_cluster) {
  if (virtual_cluster.hasObject("method")) {
    method_ = virtual_cluster.getString("method");
  }

  pattern_ = std::regex{virtual_cluster.getString("pattern"), std::regex::optimize};
  name_ = virtual_cluster.getString("name");
  priority_ = ConfigUtility::parsePriority(virtual_cluster);
}

const VirtualHostImpl* RouteMatcher::findWildcardVirtualHost(const std::string& host) const {
  // We do a longest wildcard suffix match against the host that's passed in.
  // (e.g. foo-bar.baz.com should match *-bar.baz.com before matching *.baz.com)
  // This is done by scanning the length => wildcards map looking for every
  // wildcard whose size is < length.
  for (const auto& iter : wildcard_virtual_host_suffixes_) {
    const uint32_t wildcard_length = iter.first;
    const auto& wildcard_map = iter.second;
    // >= because *.foo.com shouldn't match .foo.com.
    if (wildcard_length >= host.size()) {
      continue;
    }
    const auto& match = wildcard_map.find(host.substr(host.size() - wildcard_length));
    if (match != wildcard_map.end()) {
      return match->second.get();
    }
  }
  return nullptr;
}

RouteMatcher::RouteMatcher(const Json::Object& json_config, const ConfigImpl& global_route_config,
                           Runtime::Loader& runtime, Upstream::ClusterManager& cm,
                           bool validate_clusters) {

  json_config.validateSchema(Json::Schema::ROUTE_CONFIGURATION_SCHEMA);

  for (const Json::ObjectSharedPtr& virtual_host_config :
       json_config.getObjectArray("virtual_hosts")) {
    VirtualHostSharedPtr virtual_host(new VirtualHostImpl(*virtual_host_config, global_route_config,
                                                          runtime, cm, validate_clusters));
    uses_runtime_ |= virtual_host->usesRuntime();

    for (const std::string& domain : virtual_host_config->getStringArray("domains")) {
      if ("*" == domain) {
        if (default_virtual_host_) {
          throw EnvoyException(fmt::format("Only a single single wildcard domain is permitted"));
        }
        default_virtual_host_ = virtual_host;
      } else if (domain.size() > 0 && '*' == domain[0]) {
        wildcard_virtual_host_suffixes_[domain.size() - 1].emplace(domain.substr(1), virtual_host);
      } else {
        if (virtual_hosts_.find(domain) != virtual_hosts_.end()) {
          throw EnvoyException(fmt::format(
              "Only unique values for domains are permitted. Duplicate entry of domain {}",
              domain));
        }
        virtual_hosts_.emplace(domain, virtual_host);
      }
    }
  }
}

RouteConstSharedPtr VirtualHostImpl::getRouteFromEntries(const Http::HeaderMap& headers,
                                                         uint64_t random_value) const {
  // First check for ssl redirect.
  if (ssl_requirements_ == SslRequirements::ALL && headers.ForwardedProto()->value() != "https") {
    return SSL_REDIRECT_ROUTE;
  } else if (ssl_requirements_ == SslRequirements::EXTERNAL_ONLY &&
             headers.ForwardedProto()->value() != "https" && !headers.EnvoyInternalRequest()) {
    return SSL_REDIRECT_ROUTE;
  }

  // Check for a route that matches the request.
  for (const RouteEntryImplBaseConstSharedPtr& route : routes_) {
    RouteConstSharedPtr route_entry = route->matches(headers, random_value);
    if (nullptr != route_entry) {
      return route_entry;
    }
  }

  return nullptr;
}

const VirtualHostImpl* RouteMatcher::findVirtualHost(const Http::HeaderMap& headers) const {
  // Fast path the case where we only have a default virtual host.
  if (virtual_hosts_.empty() && default_virtual_host_) {
    return default_virtual_host_.get();
  }

  // TODO (@rshriram) Match Origin header in WebSocket
  // request with VHost, using wildcard match
  const char* host = headers.Host()->value().c_str();
  const auto& iter = virtual_hosts_.find(host);
  if (iter != virtual_hosts_.end()) {
    return iter->second.get();
  }
  if (!wildcard_virtual_host_suffixes_.empty()) {
    const VirtualHostImpl* vhost = findWildcardVirtualHost(host);
    if (vhost != nullptr) {
      return vhost;
    }
  }
  return default_virtual_host_.get();
}

RouteConstSharedPtr RouteMatcher::route(const Http::HeaderMap& headers,
                                        uint64_t random_value) const {
  const VirtualHostImpl* virtual_host = findVirtualHost(headers);
  if (virtual_host) {
    return virtual_host->getRouteFromEntries(headers, random_value);
  } else {
    return nullptr;
  }
}

const VirtualHostImpl::CatchAllVirtualCluster VirtualHostImpl::VIRTUAL_CLUSTER_CATCH_ALL;
const SslRedirector SslRedirectRoute::SSL_REDIRECTOR;
const std::shared_ptr<const SslRedirectRoute> VirtualHostImpl::SSL_REDIRECT_ROUTE{
    new SslRedirectRoute()};

const VirtualCluster*
VirtualHostImpl::virtualClusterFromEntries(const Http::HeaderMap& headers) const {
  for (const VirtualClusterEntry& entry : virtual_clusters_) {
    bool method_matches =
        !entry.method_.valid() || headers.Method()->value().c_str() == entry.method_.value();

    if (method_matches && std::regex_match(headers.Path()->value().c_str(), entry.pattern_)) {
      return &entry;
    }
  }

  if (virtual_clusters_.size() > 0) {
    return &VIRTUAL_CLUSTER_CATCH_ALL;
  }

  return nullptr;
}

ConfigImpl::ConfigImpl(const Json::Object& config, Runtime::Loader& runtime,
                       Upstream::ClusterManager& cm, bool validate_clusters_default) {
  route_matcher_.reset(
      new RouteMatcher(config, *this, runtime, cm,
                       config.getBoolean("validate_clusters", validate_clusters_default)));

  if (config.hasObject("internal_only_headers")) {
    for (const std::string& header : config.getStringArray("internal_only_headers")) {
      internal_only_headers_.push_back(Http::LowerCaseString(header));
    }
  }

  if (config.hasObject("response_headers_to_add")) {
    for (const Json::ObjectSharedPtr& header : config.getObjectArray("response_headers_to_add")) {
      response_headers_to_add_.push_back(
          {Http::LowerCaseString(header->getString("key")), header->getString("value")});
    }
  }

  if (config.hasObject("response_headers_to_remove")) {
    for (const std::string& header : config.getStringArray("response_headers_to_remove")) {
      response_headers_to_remove_.push_back(Http::LowerCaseString(header));
    }
  }

  if (config.hasObject("request_headers_to_add")) {
    for (const Json::ObjectSharedPtr& header : config.getObjectArray("request_headers_to_add")) {
      request_headers_to_add_.push_back(
          {Http::LowerCaseString(header->getString("key")), header->getString("value")});
    }
  }
}

} // namespace Router
} // namespace Envoy
