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
    const envoy::extensions::filters::network::thrift_proxy::v3::Route& route)
    : cluster_name_(route.route().cluster()),
      config_headers_(Http::HeaderUtility::buildHeaderDataVector(route.match().headers())),
      rate_limit_policy_(route.route().rate_limits()),
      strip_service_name_(route.route().strip_service_name()),
      cluster_header_(route.route().cluster_header()) {
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
    const auto& headers = metadata.headers();
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
    const envoy::extensions::filters::network::thrift_proxy::v3::Route& route)
    : RouteEntryImplBase(route), method_name_(route.match().method_name()),
      invert_(route.match().invert()) {
  if (method_name_.empty() && invert_) {
    throw EnvoyException("Cannot have an empty method name with inversion enabled");
  }
}

RouteConstSharedPtr MethodNameRouteEntryImpl::matches(const MessageMetadata& metadata,
                                                      uint64_t random_value) const {
  if (RouteEntryImplBase::headersMatch(metadata.headers())) {
    bool matches =
        method_name_.empty() || (metadata.hasMethodName() && metadata.methodName() == method_name_);

    if (matches ^ invert_) {
      return clusterEntry(random_value, metadata);
    }
  }

  return nullptr;
}

ServiceNameRouteEntryImpl::ServiceNameRouteEntryImpl(
    const envoy::extensions::filters::network::thrift_proxy::v3::Route& route)
    : RouteEntryImplBase(route), invert_(route.match().invert()) {
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
  if (RouteEntryImplBase::headersMatch(metadata.headers())) {
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
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& config) {
  using envoy::extensions::filters::network::thrift_proxy::v3::RouteMatch;

  for (const auto& route : config.routes()) {
    switch (route.match().match_specifier_case()) {
    case RouteMatch::MatchSpecifierCase::kMethodName:
      routes_.emplace_back(new MethodNameRouteEntryImpl(route));
      break;
    case RouteMatch::MatchSpecifierCase::kServiceName:
      routes_.emplace_back(new ServiceNameRouteEntryImpl(route));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
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
    upstream_request_->resetStream();
    cleanup();
  }
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
    stats().route_missing_.inc();
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::UnknownMethod,
                     fmt::format("no route for method '{}'", metadata->methodName())),
        true);
    return FilterStatus::StopIteration;
  }

  route_entry_ = route_->routeEntry();
  const std::string& cluster_name = route_entry_->clusterName();

  auto prepare_result =
      prepareUpstreamRequest(cluster_name, metadata, callbacks_->downstreamTransportType(),
                             callbacks_->downstreamProtocolType(), this);
  if (prepare_result.exception.has_value()) {
    callbacks_->sendLocalReply(prepare_result.exception.value(), true);
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
  upstream_request_ =
      std::make_unique<UpstreamRequest>(*this, *upstream_req_info.conn_pool_data, metadata,
                                        upstream_req_info.transport, upstream_req_info.protocol);
  return upstream_request_->start();
}

FilterStatus Router::messageEnd() {
  ProtocolConverter::messageEnd();
  request_size_ += upstream_request_->encodeAndWrite(upstream_request_buffer_);
  recordUpstreamRequestSize(*cluster_, request_size_);
  return FilterStatus::Continue;
}

void Router::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  const bool done =
      upstream_request_->handleUpstreamData(data, end_stream, *this, *upstream_response_callbacks_);
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
