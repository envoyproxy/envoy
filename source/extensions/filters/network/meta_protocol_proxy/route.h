#pragma once

#include <functional>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/matcher/action/v3/action.pb.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/matcher/action/v3/action.pb.validate.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/meta_protocol_proxy/v3/route.pb.validate.h"
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

using ProtoRouteAction =
    envoy::extensions::filters::network::meta_protocol_proxy::matcher::action::v3::RouteAction;
using ProtoRouteConfiguration =
    envoy::extensions::filters::network::meta_protocol_proxy::v3::RouteConfiguration;

class RouteEntryImpl : public RouteEntry {
public:
  RouteEntryImpl(const ProtoRouteAction& route,
                 Envoy::Server::Configuration::ServerFactoryContext& context);

  // RouteEntry
  const std::string& clusterName() const override { return cluster_name_; }
  const RouteSpecificFilterConfig* perFilterConfig(absl::string_view name) const override {
    auto iter = per_filter_configs_.find(name);
    return iter != per_filter_configs_.end() ? iter->second.get() : nullptr;
  }
  const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }

private:
  static const uint64_t DEFAULT_ROUTE_TIMEOUT_MS = 15000;

  std::string cluster_name_;

  const envoy::config::core::v3::Metadata metadata_;

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
  std::string name() const override { return "envoy.matching.action.meta_protocol.route"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoRouteAction>();
  }
};

class RouteMatcherImpl : public RouteMatcher, Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouteMatcherImpl(const ProtoRouteConfiguration& route_config,
                   Envoy::Server::Configuration::FactoryContext& context);

  RouteEntryConstSharedPtr routeEntry(const Request& request) const override;

  absl::string_view name() { return name_; }

private:
  std::string name_;
  Matcher::MatchTreeSharedPtr<Request> matcher_;
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
