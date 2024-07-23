#include "source/extensions/filters/network/generic_proxy/route_impl.h"

#include <memory>

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/matchers.h"
#include "source/common/config/utility.h"
#include "source/extensions/filters/network/generic_proxy/interface/filter.h"
#include "source/extensions/filters/network/generic_proxy/route.h"

#include "interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

RouteSpecificFilterConfigConstSharedPtr RouteEntryImpl::createRouteSpecificFilterConfig(
    const std::string& name, const ProtobufWkt::Any& typed_config,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {

  auto* factory = Config::Utility::getFactoryByType<NamedFilterConfigFactory>(typed_config);
  if (factory == nullptr) {
    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.no_extension_lookup_by_name")) {
      factory = Config::Utility::getFactoryByName<NamedFilterConfigFactory>(name);
    }
  }

  if (factory == nullptr) {
    ExceptionUtil::throwEnvoyException(
        fmt::format("Didn't find a registered implementation for '{}' with type URL: '{}'", name,
                    Config::Utility::getFactoryType(typed_config)));
  }

  ProtobufTypes::MessagePtr message = factory->createEmptyRouteConfigProto();
  if (message == nullptr) {
    return nullptr;
  }

  Envoy::Config::Utility::translateOpaqueConfig(typed_config, validator, *message);
  return factory->createRouteSpecificFilterConfig(*message, factory_context, validator);
}

RouteEntryImpl::RouteEntryImpl(const ProtoRouteAction& route_action,
                               Envoy::Server::Configuration::ServerFactoryContext& context)
    : name_(route_action.name()), cluster_name_(route_action.cluster()),
      metadata_(route_action.metadata()), typed_metadata_(metadata_),
      timeout_(PROTOBUF_GET_MS_OR_DEFAULT(route_action, timeout, DEFAULT_ROUTE_TIMEOUT_MS)),
      retry_policy_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route_action.retry_policy(), num_retries, 1)) {

  for (const auto& proto_filter_config : route_action.per_filter_config()) {
    auto route_config =
        createRouteSpecificFilterConfig(proto_filter_config.first, proto_filter_config.second,
                                        context, context.messageValidationVisitor());
    if (route_config != nullptr) {
      per_filter_configs_.emplace(proto_filter_config.first, std::move(route_config));
    }
  }
}

Matcher::ActionFactoryCb RouteMatchActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, RouteActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& route_action =
      MessageUtil::downcastAndValidate<const ProtoRouteAction&>(config, validation_visitor);
  auto route = std::make_shared<RouteEntryImpl>(route_action, context.factory_context);
  return [route]() { return std::make_unique<RouteMatchAction>(route); };
}
REGISTER_FACTORY(RouteMatchActionFactory, Matcher::ActionFactory<RouteActionContext>);

VirtualHostImpl::VirtualHostImpl(const ProtoVirtualHost& virtual_host_config,
                                 Envoy::Server::Configuration::ServerFactoryContext& context, bool)
    : name_(virtual_host_config.name()) {
  RouteActionValidationVisitor validation_visitor;
  RouteActionContext action_context{context};

  Matcher::MatchTreeFactory<MatchInput, RouteActionContext> factory(action_context, context,
                                                                    validation_visitor);

  matcher_ = factory.create(virtual_host_config.routes())();

  if (!validation_visitor.errors().empty()) {
    throw EnvoyException(fmt::format("requirement violation while creating route match tree: {}",
                                     validation_visitor.errors()[0]));
  }
}

RouteEntryConstSharedPtr VirtualHostImpl::routeEntry(const MatchInput& request) const {
  auto match = Matcher::evaluateMatch<MatchInput>(*matcher_, request);

  if (match.result_) {
    auto action = match.result_();

    // The only possible action that can be used within the route matching context
    // is the RouteMatchAction, so this must be true.
    ASSERT(action->typeUrl() == RouteMatchAction::staticTypeUrl());
    ASSERT(dynamic_cast<RouteMatchAction*>(action.get()));
    const RouteMatchAction& route_action = static_cast<const RouteMatchAction&>(*action);

    return route_action.route();
  }

  ENVOY_LOG(debug, "failed to match incoming request: {}",
            static_cast<uint32_t>(match.match_state_));
  return nullptr;
}

RouteMatcherImpl::RouteMatcherImpl(const ProtoRouteConfiguration& route_config,
                                   Envoy::Server::Configuration::ServerFactoryContext& context,
                                   bool validate_clusters)
    : name_(route_config.name()) {
  constexpr absl::string_view wildcard_flag{"*"};

  // TODO(wbpcode): maybe share the same code with common/router/config_impl.cc by using template.
  for (const auto& virtual_host_config : route_config.virtual_hosts()) {
    VirtualHostSharedPtr virtual_host =
        std::make_shared<VirtualHostImpl>(virtual_host_config, context, validate_clusters);
    for (const std::string& host_name : virtual_host_config.hosts()) {
      if (host_name.empty()) {
        throw EnvoyException(
            fmt::format("Invalid empty host name in route {}", route_config.name()));
      }

      absl::string_view host_name_view{host_name};
      bool duplicate_found = false;
      if (wildcard_flag == host_name_view) {
        // Catch all virtual host.
        if (default_virtual_host_ != nullptr) {
          throw EnvoyException(fmt::format("Only a single wildcard domain is permitted in route {}",
                                           route_config.name()));
        }
        default_virtual_host_ = virtual_host;
      } else if (absl::StartsWith(host_name_view, wildcard_flag)) {
        duplicate_found = !wildcard_virtual_host_suffixes_[host_name_view.size() - 1]
                               .emplace(host_name_view.substr(1), virtual_host)
                               .second;
      } else if (absl::EndsWith(host_name_view, wildcard_flag)) {
        duplicate_found =
            !wildcard_virtual_host_prefixes_[host_name_view.size() - 1]
                 .emplace(host_name_view.substr(0, host_name_view.size() - 1), virtual_host)
                 .second;
      } else {
        duplicate_found = !virtual_hosts_.emplace(host_name_view, virtual_host).second;
      }
      if (duplicate_found) {
        throw EnvoyException(fmt::format("Only unique values for host are permitted. Duplicate "
                                         "entry of domain {} in route {}",
                                         host_name_view, route_config.name()));
      }
    }
  }

  // 'routes' is supported for backwards compatibility. It will be used as default virtual host
  // if no catch-all virtual host is specified.
  if (route_config.has_routes()) {
    if (default_virtual_host_ == nullptr) {
      ProtoVirtualHost proto_virtual_host;
      proto_virtual_host.mutable_routes()->MergeFrom(route_config.routes());
      default_virtual_host_ =
          std::make_shared<VirtualHostImpl>(proto_virtual_host, context, validate_clusters);
    } else {
      throw EnvoyException(fmt::format("'routes' cannot be specified at the same time as a "
                                       "catch-all ('*') virtual host in route {}",
                                       route_config.name()));
    }
  }
}

const VirtualHostImpl* RouteMatcherImpl::findWildcardVirtualHost(
    absl::string_view host, const RouteMatcherImpl::WildcardVirtualHosts& wildcard_virtual_hosts,
    RouteMatcherImpl::SubstringFunction substring_function) const {
  // We do a longest wildcard match against the host that's passed in
  // (e.g. "foo-bar.baz.com" should match "*-bar.baz.com" before matching "*.baz.com" for suffix
  // wildcards). This is done by scanning the length => wildcards map looking for every wildcard
  // whose size is < length.
  for (const auto& iter : wildcard_virtual_hosts) {
    const uint32_t wildcard_length = iter.first;
    const auto& wildcard_map = iter.second;
    // >= because *.foo.com shouldn't match .foo.com.
    if (wildcard_length >= host.size()) {
      continue;
    }
    const auto& match = wildcard_map.find(substring_function(host, wildcard_length));
    if (match != wildcard_map.end()) {
      return match->second.get();
    }
  }
  return nullptr;
}

const VirtualHostImpl* RouteMatcherImpl::findVirtualHost(const MatchInput& request) const {
  absl::string_view host = request.requestHeader().host();

  // Fast path the case where we only have a default virtual host or host property is provided.
  if (host.empty() || (virtual_hosts_.empty() && wildcard_virtual_host_suffixes_.empty() &&
                       wildcard_virtual_host_prefixes_.empty())) {
    return default_virtual_host_.get();
  }

  const auto iter = virtual_hosts_.find(host);
  if (iter != virtual_hosts_.end()) {
    return iter->second.get();
  }

  if (!wildcard_virtual_host_suffixes_.empty()) {
    const VirtualHostImpl* vhost = findWildcardVirtualHost(
        host, wildcard_virtual_host_suffixes_,
        [](absl::string_view h, int l) -> absl::string_view { return h.substr(h.size() - l); });
    if (vhost != nullptr) {
      return vhost;
    }
  }

  if (!wildcard_virtual_host_prefixes_.empty()) {
    const VirtualHostImpl* vhost = findWildcardVirtualHost(
        host, wildcard_virtual_host_prefixes_,
        [](absl::string_view h, int l) -> absl::string_view { return h.substr(0, l); });
    if (vhost != nullptr) {
      return vhost;
    }
  }
  return default_virtual_host_.get();
}

RouteEntryConstSharedPtr RouteMatcherImpl::routeEntry(const MatchInput& request) const {
  const auto* virtual_host = findVirtualHost(request);
  if (virtual_host != nullptr) {
    return virtual_host->routeEntry(request);
  }
  return nullptr;
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
