#include "source/extensions/filters/network/thrift_proxy/router/router_impl.h"

#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/utility.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

RouteEntryImplBase::RouteEntryImplBase(
    const envoy::extensions::filters::network::thrift_proxy::v3::Route& route,
    Server::Configuration::CommonFactoryContext& context)
    : cluster_name_(route.route().cluster()),
      config_headers_(Http::HeaderUtility::buildHeaderDataVector(route.match().headers(), context)),
      rate_limit_policy_(route.route().rate_limits(), context),
      strip_service_name_(route.route().strip_service_name()),
      cluster_header_(route.route().cluster_header()),
      mirror_policies_(buildMirrorPolicies(route.route())) {
  if (route.route().has_metadata_match()) {
    const auto filter_it = route.route().metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != route.route().metadata_match().filter_metadata().end()) {
      metadata_match_criteria_ =
          std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }

  if (route.route().cluster_specifier_case() ==
      envoy::extensions::filters::network::thrift_proxy::v3::RouteAction::ClusterSpecifierCase::
          kWeightedClusters) {

    total_cluster_weight_ = 0UL;
    for (const auto& cluster : route.route().weighted_clusters().clusters()) {
      std::unique_ptr<WeightedClusterEntry> cluster_entry(new WeightedClusterEntry(*this, cluster));
      weighted_clusters_.emplace_back(std::move(cluster_entry));
      total_cluster_weight_ += weighted_clusters_.back()->clusterWeight();
    }
  }
}

// Similar validation procedure with Envoy::Router::RouteEntryImplBase::validateCluster
void RouteEntryImplBase::validateClusters(
    const Upstream::ClusterManager::ClusterInfoMaps& cluster_info_maps) const {
  // Currently, we verify that the cluster exists in the CM if we have an explicit cluster or
  // weighted cluster rule. We obviously do not verify a cluster_header rule. This means that
  // trying to use all CDS clusters with a static route table will not work. In the upcoming RDS
  // change we will make it so that dynamically loaded route tables do *not* perform CM checks.
  // In the future we might decide to also have a config option that turns off checks for static
  // route tables. This would enable the all CDS with static route table case.
  if (!cluster_name_.empty()) {
    if (!cluster_info_maps.hasCluster(cluster_name_)) {
      throw EnvoyException(fmt::format("route: unknown thrift cluster '{}'", cluster_name_));
    }
  } else if (!weighted_clusters_.empty()) {
    for (const WeightedClusterEntrySharedPtr& cluster : weighted_clusters_) {
      if (!cluster_info_maps.hasCluster(cluster->clusterName())) {
        throw EnvoyException(
            fmt::format("route: unknown thrift weighted cluster '{}'", cluster->clusterName()));
      }
    }
  }
}

std::vector<std::shared_ptr<RequestMirrorPolicy>> RouteEntryImplBase::buildMirrorPolicies(
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteAction& route) {
  std::vector<std::shared_ptr<RequestMirrorPolicy>> policies{};

  const auto& proto_policies = route.request_mirror_policies();
  policies.reserve(proto_policies.size());
  for (const auto& policy : proto_policies) {
    policies.push_back(std::make_shared<RequestMirrorPolicyImpl>(
        policy.cluster(), policy.runtime_fraction().runtime_key(),
        policy.runtime_fraction().default_value()));
  }

  return policies;
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

const RouteEntry* RouteEntryImplBase::routeEntry() const { return this; }

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(uint64_t random_value,
                                                     const MessageMetadata& metadata) const {
  if (!weighted_clusters_.empty()) {
    return WeightedClusterUtil::pickCluster(weighted_clusters_, total_cluster_weight_, random_value,
                                            false);
  }

  const auto& cluster_header = clusterHeader();
  if (!cluster_header.get().empty()) {
    const auto& headers = metadata.requestHeaders();
    const auto entry = headers.get(cluster_header);
    if (!entry.empty()) {
      // This is an implicitly untrusted header, so per the API documentation only the first
      // value is used.
      return std::make_shared<DynamicRouteEntry>(*this, entry[0]->value().getStringView());
    }

    return nullptr;
  }

  return shared_from_this();
}

bool RouteEntryImplBase::headersMatch(const Http::HeaderMap& headers) const {
  return Http::HeaderUtility::matchHeaders(headers, config_headers_);
}

RouteEntryImplBase::WeightedClusterEntry::WeightedClusterEntry(
    const RouteEntryImplBase& parent,
    const envoy::extensions::filters::network::thrift_proxy::v3::WeightedCluster::ClusterWeight&
        cluster)
    : parent_(parent), cluster_name_(cluster.name()),
      cluster_weight_(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight)) {
  if (cluster.has_metadata_match()) {
    const auto filter_it = cluster.metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != cluster.metadata_match().filter_metadata().end()) {
      if (parent.metadata_match_criteria_) {
        metadata_match_criteria_ =
            parent.metadata_match_criteria_->mergeMatchCriteria(filter_it->second);
      } else {
        metadata_match_criteria_ =
            std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
      }
    }
  }
}

MethodNameRouteEntryImpl::MethodNameRouteEntryImpl(
    const envoy::extensions::filters::network::thrift_proxy::v3::Route& route,
    Server::Configuration::CommonFactoryContext& context)
    : RouteEntryImplBase(route, context), method_name_(route.match().method_name()),
      invert_(route.match().invert()) {
  if (method_name_.empty() && invert_) {
    throw EnvoyException("Cannot have an empty method name with inversion enabled");
  }
}

RouteConstSharedPtr MethodNameRouteEntryImpl::matches(const MessageMetadata& metadata,
                                                      uint64_t random_value) const {
  if (RouteEntryImplBase::headersMatch(metadata.requestHeaders())) {
    bool matches =
        method_name_.empty() || (metadata.hasMethodName() && metadata.methodName() == method_name_);

    if (matches ^ invert_) {
      return clusterEntry(random_value, metadata);
    }
  }

  return nullptr;
}

ServiceNameRouteEntryImpl::ServiceNameRouteEntryImpl(
    const envoy::extensions::filters::network::thrift_proxy::v3::Route& route,
    Server::Configuration::CommonFactoryContext& context)
    : RouteEntryImplBase(route, context), invert_(route.match().invert()) {
  const std::string service_name = route.match().service_name();
  if (service_name.empty() && invert_) {
    throw EnvoyException("Cannot have an empty service name with inversion enabled");
  }

  if (!service_name.empty() && !absl::EndsWith(service_name, ":")) {
    service_name_ = service_name + ":";
  } else {
    service_name_ = service_name;
  }
}

RouteConstSharedPtr ServiceNameRouteEntryImpl::matches(const MessageMetadata& metadata,
                                                       uint64_t random_value) const {
  if (RouteEntryImplBase::headersMatch(metadata.requestHeaders())) {
    bool matches =
        service_name_.empty() ||
        (metadata.hasMethodName() && absl::StartsWith(metadata.methodName(), service_name_));

    if (matches ^ invert_) {
      return clusterEntry(random_value, metadata);
    }
  }

  return nullptr;
}

RouteMatcher::RouteMatcher(
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& config,
    const absl::optional<Upstream::ClusterManager::ClusterInfoMaps>& validation_clusters,
    Server::Configuration::CommonFactoryContext& context) {
  using envoy::extensions::filters::network::thrift_proxy::v3::RouteMatch;

  for (const auto& route : config.routes()) {
    RouteEntryImplBaseConstSharedPtr route_entry;
    switch (route.match().match_specifier_case()) {
    case RouteMatch::MatchSpecifierCase::kMethodName:
      route_entry = std::make_shared<MethodNameRouteEntryImpl>(route, context);
      break;
    case RouteMatch::MatchSpecifierCase::kServiceName:
      route_entry = std::make_shared<ServiceNameRouteEntryImpl>(route, context);
      break;
    case RouteMatch::MatchSpecifierCase::MATCH_SPECIFIER_NOT_SET:
      PANIC_DUE_TO_CORRUPT_ENUM;
    }

    if (validation_clusters) {
      // Throw exception for unknown clusters.
      route_entry->validateClusters(*validation_clusters);

      for (const auto& mirror_policy : route_entry->requestMirrorPolicies()) {
        if (!validation_clusters->hasCluster(mirror_policy->clusterName())) {
          throw EnvoyException(fmt::format("route: unknown thrift shadow cluster '{}'",
                                           mirror_policy->clusterName()));
        }
      }
    }

    // Now we pass the validation. Add the route to the table.
    routes_.emplace_back(route_entry);
  }
}

RouteConstSharedPtr RouteMatcher::route(const MessageMetadata& metadata,
                                        uint64_t random_value) const {
  for (const auto& route : routes_) {
    RouteConstSharedPtr route_entry = route->matches(metadata, random_value);
    if (nullptr != route_entry) {
      return route_entry;
    }
  }

  return nullptr;
}

void Router::onDestroy() {
  if (upstream_request_ != nullptr) {
    ENVOY_LOG(debug, "router on destroy reset stream");
    upstream_request_->resetStream();
    cleanup();
  }

  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().onRouterDestroy();
  }

  shadow_routers_.clear();
}

void Router::setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  upstream_response_callbacks_ = std::make_unique<UpstreamResponseCallbacksImpl>(callbacks_);

  // TODO(zuercher): handle buffer limits
}

FilterStatus Router::transportBegin(MessageMetadataSharedPtr metadata) {
  UNREFERENCED_PARAMETER(metadata);
  return FilterStatus::Continue;
}

FilterStatus Router::transportEnd() {
  upstream_request_->onRequestComplete();
  if (upstream_request_->metadata_->messageType() == MessageType::Oneway) {
    // No response expected
    upstream_request_->onResponseComplete();
    cleanup();
  }
  return FilterStatus::Continue;
}

FilterStatus Router::messageBegin(MessageMetadataSharedPtr metadata) {
  route_ = callbacks_->route();
  if (!route_) {
    ENVOY_STREAM_LOG(debug, "no route match for method '{}'", *callbacks_, metadata->methodName());
    stats().routerStats().route_missing_.inc();
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::UnknownMethod,
                     fmt::format("no route for method '{}'", metadata->methodName())),
        close_downstream_on_error_);
    return FilterStatus::StopIteration;
  }

  route_entry_ = route_->routeEntry();
  const std::string& cluster_name = route_entry_->clusterName();

  auto prepare_result =
      prepareUpstreamRequest(cluster_name, metadata, callbacks_->downstreamTransportType(),
                             callbacks_->downstreamProtocolType(), this);
  if (prepare_result.exception.has_value()) {
    callbacks_->sendLocalReply(prepare_result.exception.value(), close_downstream_on_error_);
    return FilterStatus::StopIteration;
  }

  ENVOY_STREAM_LOG(debug, "router decoding request", *callbacks_);

  if (route_entry_->stripServiceName()) {
    const auto& method = metadata->methodName();
    const auto pos = method.find(':');
    if (pos != std::string::npos) {
      metadata->setMethodName(method.substr(pos + 1));
    }
  }

  auto& upstream_req_info = prepare_result.upstream_request_info.value();
  passthrough_supported_ = upstream_req_info.passthrough_supported;

  // Prepare connections for shadow routers, if there are mirror policies configured and currently
  // enabled.
  const auto& policies = route_entry_->requestMirrorPolicies();
  if (!policies.empty()) {
    for (const auto& policy : policies) {
      if (policy->enabled(runtime_)) {
        auto shadow_router =
            shadow_writer_.submit(policy->clusterName(), metadata, upstream_req_info.transport,
                                  upstream_req_info.protocol);
        if (shadow_router.has_value()) {
          shadow_routers_.push_back(shadow_router.value());
        }
      }
    }
  }

  upstream_request_ = std::make_unique<UpstreamRequest>(
      *this, *upstream_req_info.conn_pool_data, metadata, upstream_req_info.transport,
      upstream_req_info.protocol, close_downstream_on_error_);
  return upstream_request_->start();
}

FilterStatus Router::messageEnd() {
  ProtocolConverter::messageEnd();
  const auto encode_size = upstream_request_->encodeAndWrite(upstream_request_buffer_);
  addSize(encode_size);
  stats().recordUpstreamRequestSize(*cluster_, request_size_);
  callbacks_->streamInfo().addBytesReceived(request_size_);

  // Dispatch shadow requests, if any.
  // Note: if connections aren't ready, the write will happen when appropriate.
  for (auto& shadow_router : shadow_routers_) {
    auto& router = shadow_router.get();
    router.requestOwner().messageEnd();
  }

  return FilterStatus::Continue;
}

FilterStatus Router::passthroughData(Buffer::Instance& data) {
  for (auto& shadow_router : shadow_routers_) {
    Buffer::OwnedImpl shadow_data;
    shadow_data.add(data);
    shadow_router.get().requestOwner().passthroughData(shadow_data);
  }

  return ProtocolConverter::passthroughData(data);
}

FilterStatus Router::structBegin(absl::string_view name) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().structBegin(name);
  }

  return ProtocolConverter::structBegin(name);
}

FilterStatus Router::structEnd() {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().structEnd();
  }

  return ProtocolConverter::structEnd();
}

FilterStatus Router::fieldBegin(absl::string_view name, FieldType& field_type, int16_t& field_id) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().fieldBegin(name, field_type, field_id);
  }

  return ProtocolConverter::fieldBegin(name, field_type, field_id);
}

FilterStatus Router::fieldEnd() {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().fieldEnd();
  }

  return ProtocolConverter::fieldEnd();
}

FilterStatus Router::boolValue(bool& value) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().boolValue(value);
  }

  return ProtocolConverter::boolValue(value);
}

FilterStatus Router::byteValue(uint8_t& value) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().byteValue(value);
  }

  return ProtocolConverter::byteValue(value);
}

FilterStatus Router::int16Value(int16_t& value) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().int16Value(value);
  }

  return ProtocolConverter::int16Value(value);
}

FilterStatus Router::int32Value(int32_t& value) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().int32Value(value);
  }

  return ProtocolConverter::int32Value(value);
}

FilterStatus Router::int64Value(int64_t& value) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().int64Value(value);
  }

  return ProtocolConverter::int64Value(value);
}

FilterStatus Router::doubleValue(double& value) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().doubleValue(value);
  }

  return ProtocolConverter::doubleValue(value);
}

FilterStatus Router::stringValue(absl::string_view value) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().stringValue(value);
  }

  return ProtocolConverter::stringValue(value);
}

FilterStatus Router::mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().mapBegin(key_type, value_type, size);
  }

  return ProtocolConverter::mapBegin(key_type, value_type, size);
}

FilterStatus Router::mapEnd() {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().mapEnd();
  }

  return ProtocolConverter::mapEnd();
}

FilterStatus Router::listBegin(FieldType& elem_type, uint32_t& size) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().listBegin(elem_type, size);
  }

  return ProtocolConverter::listBegin(elem_type, size);
}

FilterStatus Router::listEnd() {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().listEnd();
  }

  return ProtocolConverter::listEnd();
}

FilterStatus Router::setBegin(FieldType& elem_type, uint32_t& size) {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().setBegin(elem_type, size);
  }

  return ProtocolConverter::setBegin(elem_type, size);
}

FilterStatus Router::setEnd() {
  for (auto& shadow_router : shadow_routers_) {
    shadow_router.get().requestOwner().setEnd();
  }

  return ProtocolConverter::setEnd();
}

void Router::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  const bool done =
      upstream_request_->handleUpstreamData(data, end_stream, *upstream_response_callbacks_);
  if (done) {
    cleanup();
  }
}

void Router::onEvent(Network::ConnectionEvent event) { upstream_request_->onEvent(event); }

const Network::Connection* Router::downstreamConnection() const {
  if (callbacks_ != nullptr) {
    return callbacks_->connection();
  }

  return nullptr;
}

void Router::cleanup() { upstream_request_.reset(); }

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
