#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/matchers.h"
#include "source/common/config/metadata.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_route.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_stream.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

class SingleHeaderMatch {
public:
  SingleHeaderMatch(
      const envoy::extensions::filters::network::generic_proxy::v3::HeaderMatch& header);

  bool match(const GenericRequest& request) const;

private:
  const std::string name_;

  const bool invert_match_{};

  absl::optional<Envoy::Matchers::StringMatcherImpl> string_;
  bool present_match_{};
};

class SingleRouteMatch {
public:
  SingleRouteMatch(
      const envoy::extensions::filters::network::generic_proxy::v3::RouteMatch& matcher);

  bool match(const GenericRequest& request) const;

private:
  absl::optional<Envoy::Matchers::StringMatcherImpl> path_;
  absl::optional<Envoy::Matchers::StringMatcherImpl> method_;

  std::vector<SingleHeaderMatch> headers_;
};

class RetryPolicyImpl : public RetryPolicy {
public:
  RetryPolicyImpl(const envoy::extensions::filters::network::generic_proxy::v3::RetryPolicy&) {}

  bool shouldRetry(uint32_t, const GenericResponse*, absl::optional<GenericEvent>) const override {
    return false;
  }
  std::chrono::milliseconds timeout() const override { return {}; }
};

class RouteEntryImpl : public RouteEntry {
public:
  RouteEntryImpl(const envoy::extensions::filters::network::generic_proxy::v3::Route& route,
                 Envoy::Server::Configuration::FactoryContext& context);

  bool match(const GenericRequest& request) const { return route_match_.match(request); }

  // RouteEntry
  const std::string& clusterName() const override { return cluster_name_; }
  const RouteSpecificFilterConfig* perFilterConfig(absl::string_view name) const override {
    auto iter = per_filter_configs_.find(name);
    return iter != per_filter_configs_.end() ? iter->second.get() : nullptr;
  }
  void finalizeGenericRequest(GenericRequest&) const override {}
  void finalizeGenericResponse(GenericResponse&) const override {}
  const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }
  const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }
  std::chrono::milliseconds timeout() const override { return timeout_; };
  const RetryPolicy& retryPolicy() const override { return retry_policy_; }

private:
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  const SingleRouteMatch route_match_;

  std::string cluster_name_;

  const std::chrono::milliseconds timeout_;
  const RetryPolicyImpl retry_policy_;

  envoy::config::core::v3::Metadata metadata_;
  Envoy::Config::TypedMetadataImpl<GenericRouteTypedMetadataFactory> typed_metadata_;

  absl::flat_hash_map<std::string, RouteSpecificFilterConfigConstSharedPtr> per_filter_configs_;
};
using RouteEntryImplConstSharedPtr = std::shared_ptr<const RouteEntryImpl>;

class RouteMatcherImpl : public RouteMatcher {
public:
  RouteMatcherImpl(const envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration&
                       route_config,
                   Envoy::Server::Configuration::FactoryContext& context);

  RouteEntryConstSharedPtr routeEntry(const GenericRequest& request) const override;

private:
  std::string name_;

  using RouteEntryVectorSharedPtr = std::shared_ptr<std::vector<RouteEntryImplConstSharedPtr>>;
  absl::flat_hash_map<std::string, RouteEntryVectorSharedPtr> routes_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
