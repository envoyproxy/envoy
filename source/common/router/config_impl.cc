#include "config_impl.h"
#include "retry_state_impl.h"

#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"

namespace Router {

std::string SslRedirector::newPath(const Http::HeaderMap& headers) const {
  return Http::Utility::createSslRedirectPath(headers);
}

RetryPolicyImpl::RetryPolicyImpl(const Json::Object& config) {
  if (!config.hasObject("retry_policy")) {
    return;
  }

  num_retries_ = config.getObject("retry_policy")->getInteger("num_retries", 1);
  retry_on_ = RetryStateImpl::parseRetryOn(config.getObject("retry_policy")->getString("retry_on"));
}

ShadowPolicyImpl::ShadowPolicyImpl(const Json::Object& config) {
  if (!config.hasObject("shadow")) {
    return;
  }

  cluster_ = config.getObject("shadow")->getString("cluster");
  runtime_key_ = config.getObject("shadow")->getString("runtime_key", "");
}

Upstream::ResourcePriority ConfigUtility::parsePriority(const Json::Object& config) {
  std::string priority_string = config.getString("priority", "default");
  if (priority_string == "default") {
    return Upstream::ResourcePriority::Default;
  } else if (priority_string == "high") {
    return Upstream::ResourcePriority::High;
  } else {
    throw EnvoyException(fmt::format("invalid resource priority '{}'", priority_string));
  }
}

bool ConfigUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const std::vector<HeaderData> config_headers) {
  bool matches = true;

  if (!config_headers.empty()) {
    for (const HeaderData& cfg_header_data : config_headers) {
      const Http::HeaderEntry* header = request_headers.get(cfg_header_data.name_);
      if (cfg_header_data.value_.empty()) {
        matches &= (header != nullptr);
      } else if (!cfg_header_data.is_regex_) {
        matches &= (header != nullptr) && (header->value() == cfg_header_data.value_.c_str());
      } else {
        matches &= (header != nullptr) &&
                   std::regex_match(header->value().c_str(), cfg_header_data.regex_pattern_);
      }
      if (!matches) {
        break;
      }
    }
  }

  return matches;
}

RouteEntryImplBase::RouteEntryImplBase(const VirtualHostImpl& vhost, const Json::Object& route,
                                       Runtime::Loader& loader)
    : case_sensitive_(route.getBoolean("case_sensitive", true)),
      prefix_rewrite_(route.getString("prefix_rewrite", "")),
      host_rewrite_(route.getString("host_rewrite", "")), vhost_(vhost),
      cluster_name_(route.getString("cluster", "")),
      timeout_(route.getInteger("timeout_ms", DEFAULT_ROUTE_TIMEOUT_MS)),
      runtime_(loadRuntimeData(route)), loader_(loader),
      host_redirect_(route.getString("host_redirect", "")),
      path_redirect_(route.getString("path_redirect", "")), retry_policy_(route),
      rate_limit_policy_(route), shadow_policy_(route),
      priority_(ConfigUtility::parsePriority(route)) {

  // Check to make sure that we are either a redirect route or we have a cluster.
  if (!(isRedirect() ^ !cluster_name_.empty())) {
    throw EnvoyException("routes must be either redirects or cluster targets");
  }

  if (route.hasObject("headers")) {
    std::vector<Json::ObjectPtr> config_headers = route.getObjectArray("headers");
    for (const Json::ObjectPtr& header_map : config_headers) {
      // allow header value to be empty, allows matching to be only based on header presence.
      // Regex is an opt-in. Unless explicitly mentioned, we will use header values for exact string
      // matches.
      config_headers_.emplace_back(Http::LowerCaseString(header_map->getString("name")),
                                   header_map->getString("value", EMPTY_STRING),
                                   header_map->getBoolean("regex", false));
    }
  }
}

bool RouteEntryImplBase::matches(const Http::HeaderMap& headers, uint64_t random_value) const {
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

PrefixRouteEntryImpl::PrefixRouteEntryImpl(const VirtualHostImpl& vhost, const Json::Object& route,
                                           Runtime::Loader& loader)
    : RouteEntryImplBase(vhost, route, loader), prefix_(route.getString("prefix")) {}

void PrefixRouteEntryImpl::finalizeRequestHeaders(Http::HeaderMap& headers) const {
  RouteEntryImplBase::finalizeRequestHeaders(headers);

  finalizePathHeader(headers, prefix_);
}

bool PrefixRouteEntryImpl::matches(const Http::HeaderMap& headers, uint64_t random_value) const {
  return RouteEntryImplBase::matches(headers, random_value) &&
         StringUtil::startsWith(headers.Path()->value().c_str(), prefix_, case_sensitive_);
}

PathRouteEntryImpl::PathRouteEntryImpl(const VirtualHostImpl& vhost, const Json::Object& route,
                                       Runtime::Loader& loader)
    : RouteEntryImplBase(vhost, route, loader), path_(route.getString("path")) {}

void PathRouteEntryImpl::finalizeRequestHeaders(Http::HeaderMap& headers) const {
  RouteEntryImplBase::finalizeRequestHeaders(headers);

  finalizePathHeader(headers, path_);
}

bool PathRouteEntryImpl::matches(const Http::HeaderMap& headers, uint64_t random_value) const {
  if (RouteEntryImplBase::matches(headers, random_value)) {
    // TODO PERF: Avoid copy.
    std::string path = headers.Path()->value().c_str();
    size_t query_string_start = path.find("?");

    if (case_sensitive_) {
      return path.substr(0, query_string_start) == path_;
    } else {
      return StringUtil::caseInsensitiveCompare(path.substr(0, query_string_start).c_str(),
                                                path_.c_str()) == 0;
    }
  }

  return false;
}

VirtualHostImpl::VirtualHostImpl(const Json::Object& virtual_host, Runtime::Loader& runtime,
                                 Upstream::ClusterManager& cm)
    : name_(virtual_host.getString("name")), rate_limit_policy_(virtual_host) {

  std::string require_ssl = virtual_host.getString("require_ssl", "");
  if (require_ssl == "") {
    ssl_requirements_ = SslRequirements::NONE;
  } else if (require_ssl == "all") {
    ssl_requirements_ = SslRequirements::ALL;
  } else if (require_ssl == "external_only") {
    ssl_requirements_ = SslRequirements::EXTERNAL_ONLY;
  } else {
    throw EnvoyException(fmt::format("unknown 'require_ssl' type '{}'", require_ssl));
  }

  for (const Json::ObjectPtr& route : virtual_host.getObjectArray("routes")) {
    if (route->hasObject("prefix")) {
      routes_.emplace_back(new PrefixRouteEntryImpl(*this, *route, runtime));
    } else if (route->hasObject("path")) {
      routes_.emplace_back(new PathRouteEntryImpl(*this, *route, runtime));
    } else {
      throw EnvoyException("unknown routing configuration type");
    }

    if (!routes_.back()->isRedirect()) {
      if (!cm.get(routes_.back()->clusterName())) {
        throw EnvoyException(
            fmt::format("route: unknown cluster '{}'", routes_.back()->clusterName()));
      }
    }

    if (!routes_.back()->shadowPolicy().cluster().empty()) {
      if (!cm.get(routes_.back()->shadowPolicy().cluster())) {
        throw EnvoyException(fmt::format("route: unknown shadow cluster '{}'",
                                         routes_.back()->shadowPolicy().cluster()));
      }
    }
  }

  if (virtual_host.hasObject("virtual_clusters")) {
    for (const Json::ObjectPtr& virtual_cluster : virtual_host.getObjectArray("virtual_clusters")) {
      virtual_clusters_.push_back(VirtualClusterEntry(*virtual_cluster));
    }
  }
}

bool VirtualHostImpl::usesRuntime() const {
  bool uses = false;
  for (const RouteEntryImplBasePtr& route : routes_) {
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

RouteMatcher::RouteMatcher(const Json::Object& config, Runtime::Loader& runtime,
                           Upstream::ClusterManager& cm) {
  for (const Json::ObjectPtr& virtual_host_config : config.getObjectArray("virtual_hosts")) {
    VirtualHostPtr virtual_host(new VirtualHostImpl(*virtual_host_config, runtime, cm));
    uses_runtime_ |= virtual_host->usesRuntime();

    for (const std::string& domain : virtual_host_config->getStringArray("domains")) {
      if ("*" == domain) {
        if (default_virtual_host_) {
          throw EnvoyException(fmt::format("Only a single single wildcard domain is permitted"));
        }
        default_virtual_host_ = virtual_host;
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

const Route* VirtualHostImpl::getRouteFromEntries(const Http::HeaderMap& headers,
                                                  uint64_t random_value) const {
  // First check for ssl redirect.
  if (ssl_requirements_ == SslRequirements::ALL && headers.ForwardedProto()->value() != "https") {
    return &SSL_REDIRECT_ROUTE;
  } else if (ssl_requirements_ == SslRequirements::EXTERNAL_ONLY &&
             headers.ForwardedProto()->value() != "https" && !headers.EnvoyInternalRequest()) {
    return &SSL_REDIRECT_ROUTE;
  }

  // Check for a route that matches the request.
  for (const RouteEntryImplBasePtr& route : routes_) {
    if (route->matches(headers, random_value)) {
      return route.get();
    }
  }

  return nullptr;
}

const VirtualHostImpl* RouteMatcher::findVirtualHost(const Http::HeaderMap& headers) const {
  // Fast path the case where we only have a default virtual host.
  if (virtual_hosts_.empty() && default_virtual_host_) {
    return default_virtual_host_.get();
  }

  auto iter = virtual_hosts_.find(headers.Host()->value().c_str());
  if (iter != virtual_hosts_.end()) {
    return iter->second.get();
  } else if (default_virtual_host_) {
    return default_virtual_host_.get();
  }

  return nullptr;
}

const Route* RouteMatcher::route(const Http::HeaderMap& headers, uint64_t random_value) const {
  const VirtualHostImpl* virtual_host = findVirtualHost(headers);
  if (virtual_host) {
    return virtual_host->getRouteFromEntries(headers, random_value);
  } else {
    return nullptr;
  }
}

const VirtualHostImpl::CatchAllVirtualCluster VirtualHostImpl::VIRTUAL_CLUSTER_CATCH_ALL;
const SslRedirector SslRedirectRoute::SSL_REDIRECTOR;
const SslRedirectRoute VirtualHostImpl::SSL_REDIRECT_ROUTE;

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
                       Upstream::ClusterManager& cm) {
  route_matcher_.reset(new RouteMatcher(config, runtime, cm));

  if (config.hasObject("internal_only_headers")) {
    for (std::string header : config.getStringArray("internal_only_headers")) {
      internal_only_headers_.push_back(Http::LowerCaseString(header));
    }
  }

  if (config.hasObject("response_headers_to_add")) {
    for (const Json::ObjectPtr& header : config.getObjectArray("response_headers_to_add")) {
      response_headers_to_add_.push_back(
          {Http::LowerCaseString(header->getString("key")), header->getString("value")});
    }
  }

  if (config.hasObject("response_headers_to_remove")) {
    for (std::string header : config.getStringArray("response_headers_to_remove")) {
      response_headers_to_remove_.push_back(Http::LowerCaseString(header));
    }
  }
}

} // Router
