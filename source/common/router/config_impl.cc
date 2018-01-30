#include "common/router/config_impl.h"

#include <algorithm>
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
#include "common/common/fmt.h"
#include "common/common/hash.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/config/rds_json.h"
#include "common/config/well_known_names.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/retry_state_impl.h"

namespace Envoy {
namespace Router {

std::string SslRedirector::newPath(const Http::HeaderMap& headers) const {
  return Http::Utility::createSslRedirectPath(headers);
}

RetryPolicyImpl::RetryPolicyImpl(const envoy::api::v2::route::RouteAction& config) {
  if (!config.has_retry_policy()) {
    return;
  }

  per_try_timeout_ = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(config.retry_policy(), per_try_timeout, 0));
  num_retries_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.retry_policy(), num_retries, 1);
  retry_on_ = RetryStateImpl::parseRetryOn(config.retry_policy().retry_on());
  retry_on_ |= RetryStateImpl::parseRetryGrpcOn(config.retry_policy().retry_on());
}

CorsPolicyImpl::CorsPolicyImpl(const envoy::api::v2::route::CorsPolicy& config) {
  for (const auto& origin : config.allow_origin()) {
    allow_origin_.push_back(origin);
  }
  allow_methods_ = config.allow_methods();
  allow_headers_ = config.allow_headers();
  expose_headers_ = config.expose_headers();
  max_age_ = config.max_age();
  if (config.has_allow_credentials()) {
    allow_credentials_.value(PROTOBUF_GET_WRAPPED_REQUIRED(config, allow_credentials));
  }
  enabled_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enabled, true);
}

ShadowPolicyImpl::ShadowPolicyImpl(const envoy::api::v2::route::RouteAction& config) {
  if (!config.has_request_mirror_policy()) {
    return;
  }

  cluster_ = config.request_mirror_policy().cluster();
  runtime_key_ = config.request_mirror_policy().runtime_key();
}

class HeaderHashMethod : public HashPolicyImpl::HashMethod {
public:
  HeaderHashMethod(const std::string& header_name) : header_name_(header_name) {}

  Optional<uint64_t> evaluate(const std::string&, const Http::HeaderMap& headers,
                              const HashPolicy::AddCookieCallback) const override {
    Optional<uint64_t> hash;

    const Http::HeaderEntry* header = headers.get(header_name_);
    if (header) {
      hash.value(HashUtil::xxHash64(header->value().c_str()));
    }
    return hash;
  }

private:
  const Http::LowerCaseString header_name_;
};

class CookieHashMethod : public HashPolicyImpl::HashMethod {
public:
  CookieHashMethod(const std::string& key, long ttl) : key_(key), ttl_(ttl) {}

  Optional<uint64_t> evaluate(const std::string&, const Http::HeaderMap& headers,
                              const HashPolicy::AddCookieCallback add_cookie) const override {
    Optional<uint64_t> hash;
    std::string value = Http::Utility::parseCookieValue(headers, key_);

    if (value.empty() && ttl_ != std::chrono::seconds(0)) {
      value = add_cookie(key_, ttl_);
      hash.value(HashUtil::xxHash64(value));

    } else if (!value.empty()) {
      hash.value(HashUtil::xxHash64(value));
    }
    return hash;
  }

private:
  const std::string key_;
  const std::chrono::seconds ttl_;
};

class IpHashMethod : public HashPolicyImpl::HashMethod {
public:
  Optional<uint64_t> evaluate(const std::string& downstream_addr, const Http::HeaderMap&,
                              const HashPolicy::AddCookieCallback) const override {
    Optional<uint64_t> hash;
    if (!downstream_addr.empty()) {
      hash.value(HashUtil::xxHash64(downstream_addr));
    }
    return hash;
  }
};

HashPolicyImpl::HashPolicyImpl(
    const Protobuf::RepeatedPtrField<envoy::api::v2::route::RouteAction::HashPolicy>&
        hash_policies) {
  // TODO(htuch): Add support for cookie hash policies, #1295
  hash_impls_.reserve(hash_policies.size());

  for (auto& hash_policy : hash_policies) {
    switch (hash_policy.policy_specifier_case()) {
    case envoy::api::v2::route::RouteAction::HashPolicy::kHeader:
      hash_impls_.emplace_back(new HeaderHashMethod(hash_policy.header().header_name()));
      break;
    case envoy::api::v2::route::RouteAction::HashPolicy::kCookie:
      hash_impls_.emplace_back(
          new CookieHashMethod(hash_policy.cookie().name(), hash_policy.cookie().ttl().seconds()));
      break;
    case envoy::api::v2::route::RouteAction::HashPolicy::kConnectionProperties:
      if (hash_policy.connection_properties().source_ip()) {
        hash_impls_.emplace_back(new IpHashMethod());
      }
      break;
    default:
      throw EnvoyException(
          fmt::format("Unsupported hash policy {}", hash_policy.policy_specifier_case()));
    }
  }
}

Optional<uint64_t> HashPolicyImpl::generateHash(const std::string& downstream_addr,
                                                const Http::HeaderMap& headers,
                                                const AddCookieCallback add_cookie) const {
  Optional<uint64_t> hash;
  for (const HashMethodPtr& hash_impl : hash_impls_) {
    const Optional<uint64_t> new_hash = hash_impl->evaluate(downstream_addr, headers, add_cookie);
    if (new_hash.valid()) {
      // Rotating the old value prevents duplicate hash rules from cancelling each other out
      // and preserves all of the entropy
      const uint64_t old_value = hash.valid() ? ((hash.value() << 1) | (hash.value() >> 63)) : 0;
      hash.value(old_value ^ new_hash.value());
    }
  }
  return hash;
}

std::vector<MetadataMatchCriterionConstSharedPtr>
MetadataMatchCriteriaImpl::extractMetadataMatchCriteria(const MetadataMatchCriteriaImpl* parent,
                                                        const ProtobufWkt::Struct& matches) {
  std::vector<MetadataMatchCriterionConstSharedPtr> v;

  // Track locations of each name (from the parent) in v to make it
  // easier to replace them when the same name exists in matches.
  std::unordered_map<std::string, std::size_t> existing;

  if (parent) {
    for (const auto& it : parent->metadata_match_criteria_) {
      // v.size() is the index of the emplaced name.
      existing.emplace(it->name(), v.size());
      v.emplace_back(it);
    }
  }

  // Add values from matches, replacing name/values copied from parent.
  for (const auto it : matches.fields()) {
    const auto index_it = existing.find(it.first);
    if (index_it != existing.end()) {
      v[index_it->second] = std::make_shared<MetadataMatchCriterionImpl>(it.first, it.second);
    } else {
      v.emplace_back(std::make_shared<MetadataMatchCriterionImpl>(it.first, it.second));
    }
  }

  // Sort criteria by name to speed matching in the subset load balancer.
  // See source/docs/subset_load_balancer.md.
  std::sort(
      v.begin(), v.end(),
      [](const MetadataMatchCriterionConstSharedPtr& a,
         const MetadataMatchCriterionConstSharedPtr& b) -> bool { return a->name() < b->name(); });

  return v;
}

DecoratorImpl::DecoratorImpl(const envoy::api::v2::route::Decorator& decorator)
    : operation_(decorator.operation()) {}

void DecoratorImpl::apply(Tracing::Span& span) const {
  if (!operation_.empty()) {
    span.setOperation(operation_);
  }
}

const std::string& DecoratorImpl::getOperation() const { return operation_; }

RouteEntryImplBase::RouteEntryImplBase(const VirtualHostImpl& vhost,
                                       const envoy::api::v2::route::Route& route,
                                       Runtime::Loader& loader)
    : case_sensitive_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.match(), case_sensitive, true)),
      prefix_rewrite_(route.route().prefix_rewrite()), host_rewrite_(route.route().host_rewrite()),
      vhost_(vhost),
      auto_host_rewrite_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route(), auto_host_rewrite, false)),
      use_websocket_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route(), use_websocket, false)),
      cluster_name_(route.route().cluster()), cluster_header_name_(route.route().cluster_header()),
      cluster_not_found_response_code_(ConfigUtility::parseClusterNotFoundResponseCode(
          route.route().cluster_not_found_response_code())),
      timeout_(PROTOBUF_GET_MS_OR_DEFAULT(route.route(), timeout, DEFAULT_ROUTE_TIMEOUT_MS)),
      runtime_(loadRuntimeData(route.match())), loader_(loader),
      host_redirect_(route.redirect().host_redirect()),
      path_redirect_(route.redirect().path_redirect()), retry_policy_(route.route()),
      rate_limit_policy_(route.route().rate_limits()), shadow_policy_(route.route()),
      priority_(ConfigUtility::parsePriority(route.route().priority())),
      total_cluster_weight_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route().weighted_clusters(), total_weight, 100UL)),
      request_headers_parser_(HeaderParser::configure(route.route().request_headers_to_add())),
      response_headers_parser_(HeaderParser::configure(route.route().response_headers_to_add(),
                                                       route.route().response_headers_to_remove())),
      opaque_config_(parseOpaqueConfig(route)), decorator_(parseDecorator(route)),
      direct_response_code_(ConfigUtility::parseDirectResponseCode(route)),
      direct_response_body_(ConfigUtility::parseDirectResponseBody(route)) {
  if (route.route().has_metadata_match()) {
    const auto filter_it = route.route().metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != route.route().metadata_match().filter_metadata().end()) {
      metadata_match_criteria_.reset(new MetadataMatchCriteriaImpl(filter_it->second));
    }
  }

  if (route.has_metadata()) {
    metadata_ = route.metadata();
  }

  // If this is a weighted_cluster, we create N internal route entries
  // (called WeightedClusterEntry), such that each object is a simple
  // single cluster, pointing back to the parent. Metadata criteria
  // from the weighted cluster (if any) are merged with and override
  // the criteria from the route.
  if (route.route().cluster_specifier_case() ==
      envoy::api::v2::route::RouteAction::kWeightedClusters) {
    ASSERT(total_cluster_weight_ > 0);

    uint64_t total_weight = 0UL;
    const std::string& runtime_key_prefix = route.route().weighted_clusters().runtime_key_prefix();

    for (const auto& cluster : route.route().weighted_clusters().clusters()) {
      const std::string& cluster_name = cluster.name();

      MetadataMatchCriteriaImplConstPtr cluster_metadata_match_criteria;
      if (cluster.has_metadata_match()) {
        const auto filter_it = cluster.metadata_match().filter_metadata().find(
            Envoy::Config::MetadataFilters::get().ENVOY_LB);
        if (filter_it != cluster.metadata_match().filter_metadata().end()) {
          if (metadata_match_criteria_) {
            cluster_metadata_match_criteria =
                metadata_match_criteria_->mergeMatchCriteria(filter_it->second);
          } else {
            cluster_metadata_match_criteria.reset(new MetadataMatchCriteriaImpl(filter_it->second));
          }
        }
      }

      std::unique_ptr<WeightedClusterEntry> cluster_entry(
          new WeightedClusterEntry(this, runtime_key_prefix + "." + cluster_name, loader_,
                                   cluster_name, PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight),
                                   std::move(cluster_metadata_match_criteria)));
      weighted_clusters_.emplace_back(std::move(cluster_entry));
      total_weight += weighted_clusters_.back()->clusterWeight();
    }

    if (total_weight != total_cluster_weight_) {
      throw EnvoyException(fmt::format("Sum of weights in the weighted_cluster should add up to {}",
                                       total_cluster_weight_));
    }
  }

  for (const auto& header_map : route.match().headers()) {
    config_headers_.push_back(header_map);
  }

  for (const auto& query_parameter : route.match().query_parameters()) {
    config_query_parameters_.push_back(query_parameter);
  }

  if (!route.route().hash_policy().empty()) {
    hash_policy_.reset(new HashPolicyImpl(route.route().hash_policy()));
  }

  // Only set include_vh_rate_limits_ to true if the rate limit policy for the route is empty
  // or the route set `include_vh_rate_limits` to true.
  include_vh_rate_limits_ =
      (rate_limit_policy_.empty() ||
       PROTOBUF_GET_WRAPPED_OR_DEFAULT(route.route(), include_vh_rate_limits, false));

  if (route.route().has_cors()) {
    cors_policy_.reset(new CorsPolicyImpl(route.route().cors()));
  }
}

bool RouteEntryImplBase::matchRoute(const Http::HeaderMap& headers, uint64_t random_value) const {
  bool matches = true;

  if (runtime_.valid()) {
    matches &= loader_.snapshot().featureEnabled(runtime_.value().key_, runtime_.value().default_,
                                                 random_value);
  }

  matches &= ConfigUtility::matchHeaders(headers, config_headers_);
  if (!config_query_parameters_.empty()) {
    Http::Utility::QueryParams query_parameters =
        Http::Utility::parseQueryString(headers.Path()->value().c_str());
    matches &= ConfigUtility::matchQueryParams(query_parameters, config_query_parameters_);
  }

  return matches;
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

void RouteEntryImplBase::finalizeRequestHeaders(
    Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info) const {
  // Append user-specified request headers in the following order: route-level headers,
  // virtual host level headers and finally global connection manager level headers.
  request_headers_parser_->evaluateHeaders(headers, request_info);
  vhost_.requestHeaderParser().evaluateHeaders(headers, request_info);
  vhost_.globalRouteConfig().requestHeaderParser().evaluateHeaders(headers, request_info);
  if (host_rewrite_.empty()) {
    return;
  }
  headers.Host()->value(host_rewrite_);
}

void RouteEntryImplBase::finalizeResponseHeaders(
    Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info) const {
  response_headers_parser_->evaluateHeaders(headers, request_info);
  vhost_.responseHeaderParser().evaluateHeaders(headers, request_info);
  vhost_.globalRouteConfig().responseHeaderParser().evaluateHeaders(headers, request_info);
}

Optional<RouteEntryImplBase::RuntimeData>
RouteEntryImplBase::loadRuntimeData(const envoy::api::v2::route::RouteMatch& route_match) {
  Optional<RuntimeData> runtime;
  if (route_match.has_runtime()) {
    RuntimeData data;
    data.key_ = route_match.runtime().runtime_key();
    data.default_ = route_match.runtime().default_value();
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
  ASSERT(isDirectResponse());

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
RouteEntryImplBase::parseOpaqueConfig(const envoy::api::v2::route::Route& route) {
  std::multimap<std::string, std::string> ret;
  if (route.has_metadata()) {
    const auto filter_metadata =
        route.metadata().filter_metadata().find(Envoy::Config::HttpFilterNames::get().ROUTER);
    if (filter_metadata == route.metadata().filter_metadata().end()) {
      return ret;
    }
    for (auto it : filter_metadata->second.fields()) {
      if (it.second.kind_case() == ProtobufWkt::Value::kStringValue) {
        ret.emplace(it.first, it.second.string_value());
      }
    }
  }
  return ret;
}

DecoratorConstPtr RouteEntryImplBase::parseDecorator(const envoy::api::v2::route::Route& route) {
  DecoratorConstPtr ret;
  if (route.has_decorator()) {
    ret = DecoratorConstPtr(new DecoratorImpl(route.decorator()));
  }
  return ret;
}

const DirectResponseEntry* RouteEntryImplBase::directResponseEntry() const {
  // A route for a request can exclusively be a route entry, a direct response entry,
  // or a redirect entry.
  if (isDirectResponse()) {
    return this;
  } else {
    return nullptr;
  }
}

const RouteEntry* RouteEntryImplBase::routeEntry() const {
  // A route for a request can exclusively be a route entry, a direct response entry,
  // or a redirect entry.
  if (isDirectResponse()) {
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
    if (!cluster_name_.empty() || isDirectResponse()) {
      return shared_from_this();
    } else {
      ASSERT(!cluster_header_name_.get().empty());
      const Http::HeaderEntry* entry = headers.get(cluster_header_name_);
      std::string final_cluster_name;
      if (entry) {
        final_cluster_name = entry->value().c_str();
      }

      // NOTE: Though we return a shared_ptr here, the current ownership model assumes that
      //       the route table sticks around. See snapped_route_config_ in
      //       ConnectionManagerImpl::ActiveStream.
      return std::make_shared<DynamicRouteEntry>(this, final_cluster_name);
    }
  }

  uint64_t selected_value = random_value % total_cluster_weight_;
  uint64_t begin = 0UL;
  uint64_t end = 0UL;

  // Find the right cluster to route to based on the interval in which
  // the selected value falls. The intervals are determined as
  // [0, cluster1_weight), [cluster1_weight, cluster1_weight+cluster2_weight),..
  for (const WeightedClusterEntrySharedPtr& cluster : weighted_clusters_) {
    end = begin + cluster->clusterWeight();
    if (((selected_value >= begin) && (selected_value < end)) || (end >= total_cluster_weight_)) {
      // end > total_cluster_weight_: This case can only occur with Runtimes, when the user
      // specifies invalid weights such that sum(weights) > total_cluster_weight_. In this case,
      // terminate the search and just return the cluster whose weight caused the overflow.
      return cluster;
    }
    begin = end;
  }
  NOT_REACHED;
}

void RouteEntryImplBase::validateClusters(Upstream::ClusterManager& cm) const {
  if (isDirectResponse()) {
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

PrefixRouteEntryImpl::PrefixRouteEntryImpl(const VirtualHostImpl& vhost,
                                           const envoy::api::v2::route::Route& route,
                                           Runtime::Loader& loader)
    : RouteEntryImplBase(vhost, route, loader), prefix_(route.match().prefix()) {}

void PrefixRouteEntryImpl::finalizeRequestHeaders(
    Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info) const {
  RouteEntryImplBase::finalizeRequestHeaders(headers, request_info);

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

PathRouteEntryImpl::PathRouteEntryImpl(const VirtualHostImpl& vhost,
                                       const envoy::api::v2::route::Route& route,
                                       Runtime::Loader& loader)
    : RouteEntryImplBase(vhost, route, loader), path_(route.match().path()) {}

void PathRouteEntryImpl::finalizeRequestHeaders(
    Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info) const {
  RouteEntryImplBase::finalizeRequestHeaders(headers, request_info);

  finalizePathHeader(headers, path_);
}

RouteConstSharedPtr PathRouteEntryImpl::matches(const Http::HeaderMap& headers,
                                                uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, random_value)) {
    const Http::HeaderString& path = headers.Path()->value();
    const char* query_string_start = Http::Utility::findQueryStringStart(path);
    size_t compare_length = path.size();
    if (query_string_start != nullptr) {
      compare_length = query_string_start - path.c_str();
    }

    if (compare_length != path_.size()) {
      return nullptr;
    }

    if (case_sensitive_) {
      if (0 == strncmp(path.c_str(), path_.c_str(), compare_length)) {
        return clusterEntry(headers, random_value);
      }
    } else {
      if (0 == strncasecmp(path.c_str(), path_.c_str(), compare_length)) {
        return clusterEntry(headers, random_value);
      }
    }
  }

  return nullptr;
}

RegexRouteEntryImpl::RegexRouteEntryImpl(const VirtualHostImpl& vhost,
                                         const envoy::api::v2::route::Route& route,
                                         Runtime::Loader& loader)
    : RouteEntryImplBase(vhost, route, loader),
      regex_(RegexUtil::parseRegex(route.match().regex().c_str())) {}

void RegexRouteEntryImpl::finalizeRequestHeaders(
    Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info) const {
  RouteEntryImplBase::finalizeRequestHeaders(headers, request_info);

  const Http::HeaderString& path = headers.Path()->value();
  const char* query_string_start = Http::Utility::findQueryStringStart(path);
  ASSERT(std::regex_match(path.c_str(), query_string_start, regex_));
  std::string matched_path(path.c_str(), query_string_start);
  finalizePathHeader(headers, matched_path);
}

RouteConstSharedPtr RegexRouteEntryImpl::matches(const Http::HeaderMap& headers,
                                                 uint64_t random_value) const {
  if (RouteEntryImplBase::matchRoute(headers, random_value)) {
    const Http::HeaderString& path = headers.Path()->value();
    const char* query_string_start = Http::Utility::findQueryStringStart(path);
    if (std::regex_match(path.c_str(), query_string_start, regex_)) {
      return clusterEntry(headers, random_value);
    }
  }
  return nullptr;
}

VirtualHostImpl::VirtualHostImpl(const envoy::api::v2::route::VirtualHost& virtual_host,
                                 const ConfigImpl& global_route_config, Runtime::Loader& runtime,
                                 Upstream::ClusterManager& cm, bool validate_clusters)
    : name_(virtual_host.name()), rate_limit_policy_(virtual_host.rate_limits()),
      global_route_config_(global_route_config),
      request_headers_parser_(HeaderParser::configure(virtual_host.request_headers_to_add())),
      response_headers_parser_(HeaderParser::configure(virtual_host.response_headers_to_add(),
                                                       virtual_host.response_headers_to_remove())) {
  switch (virtual_host.require_tls()) {
  case envoy::api::v2::route::VirtualHost::NONE:
    ssl_requirements_ = SslRequirements::NONE;
    break;
  case envoy::api::v2::route::VirtualHost::EXTERNAL_ONLY:
    ssl_requirements_ = SslRequirements::EXTERNAL_ONLY;
    break;
  case envoy::api::v2::route::VirtualHost::ALL:
    ssl_requirements_ = SslRequirements::ALL;
    break;
  default:
    NOT_REACHED;
  }

  for (const auto& route : virtual_host.routes()) {
    const bool has_prefix =
        route.match().path_specifier_case() == envoy::api::v2::route::RouteMatch::kPrefix;
    const bool has_path =
        route.match().path_specifier_case() == envoy::api::v2::route::RouteMatch::kPath;
    const bool has_regex =
        route.match().path_specifier_case() == envoy::api::v2::route::RouteMatch::kRegex;
    if (has_prefix) {
      routes_.emplace_back(new PrefixRouteEntryImpl(*this, route, runtime));
    } else if (has_path) {
      routes_.emplace_back(new PathRouteEntryImpl(*this, route, runtime));
    } else {
      ASSERT(has_regex);
      UNREFERENCED_PARAMETER(has_regex);
      routes_.emplace_back(new RegexRouteEntryImpl(*this, route, runtime));
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

  for (const auto& virtual_cluster : virtual_host.virtual_clusters()) {
    virtual_clusters_.push_back(VirtualClusterEntry(virtual_cluster));
  }

  if (virtual_host.has_cors()) {
    cors_policy_.reset(new CorsPolicyImpl(virtual_host.cors()));
  }
}

VirtualHostImpl::VirtualClusterEntry::VirtualClusterEntry(
    const envoy::api::v2::route::VirtualCluster& virtual_cluster) {
  if (virtual_cluster.method() != envoy::api::v2::RequestMethod::METHOD_UNSPECIFIED) {
    method_ = envoy::api::v2::RequestMethod_Name(virtual_cluster.method());
  }

  const std::string pattern = virtual_cluster.pattern();
  pattern_ = RegexUtil::parseRegex(pattern);
  name_ = virtual_cluster.name();
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

RouteMatcher::RouteMatcher(const envoy::api::v2::RouteConfiguration& route_config,
                           const ConfigImpl& global_route_config, Runtime::Loader& runtime,
                           Upstream::ClusterManager& cm, bool validate_clusters) {
  for (const auto& virtual_host_config : route_config.virtual_hosts()) {
    VirtualHostSharedPtr virtual_host(new VirtualHostImpl(virtual_host_config, global_route_config,
                                                          runtime, cm, validate_clusters));
    for (const std::string& domain : virtual_host_config.domains()) {
      if ("*" == domain) {
        if (default_virtual_host_) {
          throw EnvoyException(fmt::format("Only a single wildcard domain is permitted"));
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

ConfigImpl::ConfigImpl(const envoy::api::v2::RouteConfiguration& config, Runtime::Loader& runtime,
                       Upstream::ClusterManager& cm, bool validate_clusters_default) {
  route_matcher_.reset(new RouteMatcher(
      config, *this, runtime, cm,
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, validate_clusters, validate_clusters_default)));

  for (const std::string& header : config.internal_only_headers()) {
    internal_only_headers_.push_back(Http::LowerCaseString(header));
  }

  request_headers_parser_ = HeaderParser::configure(config.request_headers_to_add());
  response_headers_parser_ = HeaderParser::configure(config.response_headers_to_add(),
                                                     config.response_headers_to_remove());
}

} // namespace Router
} // namespace Envoy
