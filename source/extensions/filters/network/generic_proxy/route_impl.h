#pragma once

#include <chrono>
#include <functional>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/extensions/filters/network/generic_proxy/action/v3/action.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/action/v3/action.pb.validate.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/generic_proxy/v3/route.pb.validate.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/matchers.h"
#include "source/common/config/metadata.h"
#include "source/common/http/header_utility.h"
#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/network/generic_proxy/interface/stream.h"
#include "source/extensions/filters/network/generic_proxy/match_input.h"
#include "source/extensions/filters/network/generic_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using ProtoRouteAction =
    envoy::extensions::filters::network::generic_proxy::action::v3::RouteAction;
using ProtoRouteConfiguration =
    envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration;
using ProtoVirtualHost = envoy::extensions::filters::network::generic_proxy::v3::VirtualHost;
using ProtoRetryPolicy = envoy::config::core::v3::RetryPolicy;

class RouteEntryImpl : public RouteEntry {
public:
  RouteEntryImpl(const ProtoRouteAction& route,
                 Envoy::Server::Configuration::ServerFactoryContext& context);

  // RouteEntry
  absl::string_view name() const override { return name_; }
  const std::string& clusterName() const override { return cluster_name_; }
  const RouteSpecificFilterConfig* perFilterConfig(absl::string_view name) const override {
    auto iter = per_filter_configs_.find(name);
    return iter != per_filter_configs_.end() ? iter->second.get() : nullptr;
  }
  const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }

  const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; };

  const std::chrono::milliseconds timeout() const override { return timeout_; };
  const RetryPolicy& retryPolicy() const override { return retry_policy_; }

  RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const std::string& name, const ProtobufWkt::Any& typed_config,
                                  Server::Configuration::ServerFactoryContext& factory_context,
                                  ProtobufMessage::ValidationVisitor& validator);

private:
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  std::string name_;
  std::string cluster_name_;

  const envoy::config::core::v3::Metadata metadata_;
  const Envoy::Config::TypedMetadataImpl<RouteTypedMetadataFactory> typed_metadata_;

  const std::chrono::milliseconds timeout_;
  const RetryPolicy retry_policy_;

  absl::flat_hash_map<std::string, RouteSpecificFilterConfigConstSharedPtr> per_filter_configs_;
};
using RouteEntryImplConstSharedPtr = std::shared_ptr<const RouteEntryImpl>;

struct RouteActionContext {
  Server::Configuration::ServerFactoryContext& factory_context;
};

// Action used with the matching tree to specify route to use for an incoming stream.
class RouteMatchAction : public Matcher::ActionBase<ProtoRouteAction> {
public:
  explicit RouteMatchAction(RouteEntryConstSharedPtr route) : route_(std::move(route)) {}

  RouteEntryConstSharedPtr route() const { return route_; }

private:
  RouteEntryConstSharedPtr route_;
};

class RouteActionValidationVisitor : public Matcher::MatchTreeValidationVisitor<MatchInput> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<MatchInput>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

// Registered factory for RouteMatchAction.
class RouteMatchActionFactory : public Matcher::ActionFactory<RouteActionContext> {
public:
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, RouteActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;
  std::string name() const override { return "envoy.matching.action.generic_proxy.route"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoRouteAction>();
  }
};

class NullRouteMatcherImpl : public RouteMatcher {
public:
  // RouteMatcher
  RouteEntryConstSharedPtr routeEntry(const MatchInput&) const override { return nullptr; }
};

class VirtualHostImpl : Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  VirtualHostImpl(const ProtoVirtualHost& virtual_host_config,
                  Envoy::Server::Configuration::ServerFactoryContext& context,
                  bool validate_clusters_default = false);

  RouteEntryConstSharedPtr routeEntry(const MatchInput& request) const;
  absl::string_view name() const { return name_; }

private:
  std::string name_;
  Matcher::MatchTreeSharedPtr<MatchInput> matcher_;
};
using VirtualHostSharedPtr = std::shared_ptr<VirtualHostImpl>;

class RouteMatcherImpl : public RouteMatcher, Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouteMatcherImpl(const ProtoRouteConfiguration& route_config,
                   Envoy::Server::Configuration::ServerFactoryContext& context,
                   bool validate_clusters_default = false);

  RouteEntryConstSharedPtr routeEntry(const MatchInput& request) const override;

  absl::string_view name() const { return name_; }

  const VirtualHostImpl* findVirtualHost(const MatchInput& request) const;

private:
  using WildcardVirtualHosts =
      std::map<int64_t, absl::flat_hash_map<std::string, VirtualHostSharedPtr>, std::greater<>>;
  using SubstringFunction = std::function<absl::string_view(absl::string_view, int)>;
  const VirtualHostImpl* findWildcardVirtualHost(absl::string_view host,
                                                 const WildcardVirtualHosts& wildcard_virtual_hosts,
                                                 SubstringFunction substring_function) const;

  std::string name_;

  absl::flat_hash_map<std::string, VirtualHostSharedPtr> virtual_hosts_;
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
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
