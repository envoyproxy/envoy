#pragma once

#include <functional>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/v3/route.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/matchers.h"
#include "source/common/config/metadata.h"
#include "source/common/http/header_utility.h"
#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/route.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

class RetryPolicyImpl : public RetryPolicy {
public:
  RetryPolicyImpl(
      const envoy::extensions::filters::network::meta_protocol_proxy::v3::RetryPolicy&) {}

  bool shouldRetry(uint32_t, const Response*, absl::optional<Event>) const override {
    return false;
  }
  std::chrono::milliseconds timeout() const override { return {}; }
};

class RouteEntryImpl : public RouteEntry {
public:
  RouteEntryImpl(
      const envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteAction& route,
      Envoy::Server::Configuration::ServerFactoryContext& context);

  // RouteEntry
  const std::string& clusterName() const override { return cluster_name_; }
  const RouteSpecificFilterConfig* perFilterConfig(absl::string_view name) const override {
    auto iter = per_filter_configs_.find(name);
    return iter != per_filter_configs_.end() ? iter->second.get() : nullptr;
  }
  void finalizeRequest(Request&) const override {}
  void finalizeResponse(Response&) const override {}
  const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }
  const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }
  std::chrono::milliseconds timeout() const override { return timeout_; };
  const RetryPolicy& retryPolicy() const override { return retry_policy_; }

private:
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  std::string cluster_name_;

  const std::chrono::milliseconds timeout_;
  const RetryPolicyImpl retry_policy_;

  envoy::config::core::v3::Metadata metadata_;
  Envoy::Config::TypedMetadataImpl<RouteTypedMetadataFactory> typed_metadata_;

  absl::flat_hash_map<std::string, RouteSpecificFilterConfigConstSharedPtr> per_filter_configs_;
};
using RouteEntryImplConstSharedPtr = std::shared_ptr<const RouteEntryImpl>;

struct RouteActionContext {
  Server::Configuration::ServerFactoryContext& factory_context;
};

// Action used with the matching tree to specify route to use for an incoming stream.
class RouteMatchAction
    : public Matcher::ActionBase<
          envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteAction> {
public:
  explicit RouteMatchAction(RouteEntryImplConstSharedPtr route) : route_(std::move(route)) {}

  RouteEntryImplConstSharedPtr route() const { return route_; }

private:
  const RouteEntryImplConstSharedPtr route_;
};

class RouteActionValidationVisitor : public Matcher::MatchTreeValidationVisitor<Request> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Request>&,
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
  std::string name() const override { return "route"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteAction>();
  }
};

class RouteMatcherImpl : public RouteMatcher, Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouteMatcherImpl(
      const envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteConfiguration&
          route_config,
      Envoy::Server::Configuration::FactoryContext& context);

  RouteEntryConstSharedPtr routeEntry(const Request& request) const override;

private:
  std::string name_;
  Matcher::MatchTreeSharedPtr<Request> matcher_;
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
