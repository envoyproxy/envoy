#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/http/header_utility.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/extensions/filters/network/thrift_proxy/conn_manager.h"
#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_ratelimit_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/upstream_request.h"
#include "source/extensions/filters/network/thrift_proxy/thrift_object.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RequestMirrorPolicyImpl : public RequestMirrorPolicy {
public:
  RequestMirrorPolicyImpl(const std::string& cluster_name, const std::string& runtime_key,
                          const envoy::type::v3::FractionalPercent& default_value)
      : cluster_name_(cluster_name), runtime_key_(runtime_key), default_value_(default_value) {}

  // Router::RequestMirrorPolicy
  const std::string& clusterName() const override { return cluster_name_; }
  bool enabled(Runtime::Loader& runtime) const override {
    return runtime_key_.empty() ? true
                                : runtime.snapshot().featureEnabled(runtime_key_, default_value_);
  }

private:
  const std::string cluster_name_;
  const std::string runtime_key_;
  const envoy::type::v3::FractionalPercent default_value_;
};

class RouteEntryImplBase : public RouteEntry,
                           public Route,
                           public std::enable_shared_from_this<RouteEntryImplBase> {
public:
  RouteEntryImplBase(const envoy::extensions::filters::network::thrift_proxy::v3::Route& route,
                     Server::Configuration::CommonFactoryContext& context);

  void validateClusters(const Upstream::ClusterManager::ClusterInfoMaps& cluster_info_maps) const;
  // Router::RouteEntry
  const std::string& clusterName() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_criteria_.get();
  }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  bool stripServiceName() const override { return strip_service_name_; };
  const Http::LowerCaseString& clusterHeader() const override { return cluster_header_; }
  const std::vector<std::shared_ptr<RequestMirrorPolicy>>& requestMirrorPolicies() const override {
    return mirror_policies_;
  }

  // Router::Route
  const RouteEntry* routeEntry() const override;

  virtual RouteConstSharedPtr matches(const MessageMetadata& metadata,
                                      uint64_t random_value) const PURE;

protected:
  RouteConstSharedPtr clusterEntry(uint64_t random_value, const MessageMetadata& metadata) const;
  bool headersMatch(const Http::HeaderMap& headers) const;

private:
  class WeightedClusterEntry : public RouteEntry, public Route {
  public:
    WeightedClusterEntry(
        const RouteEntryImplBase& parent,
        const envoy::extensions::filters::network::thrift_proxy::v3::WeightedCluster::ClusterWeight&
            cluster);

    uint64_t clusterWeight() const { return cluster_weight_; }

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
      if (metadata_match_criteria_) {
        return metadata_match_criteria_.get();
      }

      return parent_.metadataMatchCriteria();
    }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_.rateLimitPolicy(); }
    bool stripServiceName() const override { return parent_.stripServiceName(); }
    const Http::LowerCaseString& clusterHeader() const override {
      // Weighted cluster entries don't have a cluster header based on proto.
      ASSERT(parent_.clusterHeader().get().empty());
      return parent_.clusterHeader();
    }
    const std::vector<std::shared_ptr<RequestMirrorPolicy>>&
    requestMirrorPolicies() const override {
      return parent_.requestMirrorPolicies();
    }

    // Router::Route
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase& parent_;
    const std::string cluster_name_;
    const uint64_t cluster_weight_;
    Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  };
  using WeightedClusterEntrySharedPtr = std::shared_ptr<WeightedClusterEntry>;

  class DynamicRouteEntry : public RouteEntry, public Route {
  public:
    DynamicRouteEntry(const RouteEntryImplBase& parent, absl::string_view cluster_name)
        : parent_(parent), cluster_name_(std::string(cluster_name)) {}

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
      return parent_.metadataMatchCriteria();
    }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_.rateLimitPolicy(); }
    bool stripServiceName() const override { return parent_.stripServiceName(); }
    const Http::LowerCaseString& clusterHeader() const override { return parent_.clusterHeader(); }
    const std::vector<std::shared_ptr<RequestMirrorPolicy>>&
    requestMirrorPolicies() const override {
      return parent_.requestMirrorPolicies();
    }

    // Router::Route
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase& parent_;
    const std::string cluster_name_;
  };

  static std::vector<std::shared_ptr<RequestMirrorPolicy>> buildMirrorPolicies(
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteAction& route);

  const std::string cluster_name_;
  const std::vector<Http::HeaderUtility::HeaderDataPtr> config_headers_;
  std::vector<WeightedClusterEntrySharedPtr> weighted_clusters_;
  uint64_t total_cluster_weight_;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  const RateLimitPolicyImpl rate_limit_policy_;
  const bool strip_service_name_;
  const Http::LowerCaseString cluster_header_;
  const std::vector<std::shared_ptr<RequestMirrorPolicy>> mirror_policies_;
};

using RouteEntryImplBaseConstSharedPtr = std::shared_ptr<const RouteEntryImplBase>;

class MethodNameRouteEntryImpl : public RouteEntryImplBase {
public:
  MethodNameRouteEntryImpl(
      const envoy::extensions::filters::network::thrift_proxy::v3::Route& route,
      Server::Configuration::CommonFactoryContext& context);

  // RouteEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  const std::string method_name_;
  const bool invert_;
};

class ServiceNameRouteEntryImpl : public RouteEntryImplBase {
public:
  ServiceNameRouteEntryImpl(
      const envoy::extensions::filters::network::thrift_proxy::v3::Route& route,
      Server::Configuration::CommonFactoryContext& context);

  // RouteEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  std::string service_name_;
  const bool invert_;
};

class RouteMatcher {
public:
  // validation_clusters = absl::nullopt means that clusters are not validated.
  RouteMatcher(
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& config,
      const absl::optional<Upstream::ClusterManager::ClusterInfoMaps>& validation_clusters,
      Server::Configuration::CommonFactoryContext& context);

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const;

private:
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
};

// Adapter from DecoderFilterCallbacks to UpstreamResponseCallbacks.
class UpstreamResponseCallbacksImpl : public UpstreamResponseCallbacks {
public:
  UpstreamResponseCallbacksImpl(ThriftFilters::DecoderFilterCallbacks* callbacks)
      : callbacks_(callbacks) {}

  void startUpstreamResponse(Transport& transport, Protocol& protocol) override {
    callbacks_->startUpstreamResponse(transport, protocol);
  }
  ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& buffer) override {
    callbacks_->streamInfo().addBytesSent(buffer.length());
    return callbacks_->upstreamData(buffer);
  }
  MessageMetadataSharedPtr responseMetadata() override { return callbacks_->responseMetadata(); }
  bool responseSuccess() override { return callbacks_->responseSuccess(); }

private:
  ThriftFilters::DecoderFilterCallbacks* callbacks_{};
};

class Router : public Tcp::ConnectionPool::UpstreamCallbacks,
               public Upstream::LoadBalancerContextBase,
               public RequestOwner,
               public ThriftFilters::DecoderFilter {
public:
  Router(Upstream::ClusterManager& cluster_manager, const RouterStats& stats,
         Runtime::Loader& runtime, ShadowWriter& shadow_writer, bool close_downstream_on_error)
      : RequestOwner(cluster_manager, stats), passthrough_supported_(false), runtime_(runtime),
        shadow_writer_(shadow_writer), close_downstream_on_error_(close_downstream_on_error) {}

  ~Router() override = default;

  // ThriftFilters::DecoderFilter
  void onDestroy() override;
  void setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks& callbacks) override;
  bool passthroughSupported() const override { return passthrough_supported_; }

  // RequestOwner
  Tcp::ConnectionPool::UpstreamCallbacks& upstreamCallbacks() override {
    ASSERT(callbacks_ != nullptr);
    ASSERT(upstream_request_ != nullptr);

    auto upstream_info = std::make_shared<StreamInfo::UpstreamInfoImpl>();
    upstream_info->setUpstreamHost(upstream_request_->upstream_host_);
    callbacks_->streamInfo().setUpstreamInfo(std::move(upstream_info));

    return *this;
  }
  Buffer::OwnedImpl& buffer() override { return upstream_request_buffer_; }
  Event::Dispatcher& dispatcher() override { return callbacks_->dispatcher(); }
  void addSize(uint64_t size) override { request_size_ += size; }
  void continueDecoding() override { callbacks_->continueDecoding(); }
  void resetDownstreamConnection() override { callbacks_->resetDownstreamConnection(); }
  void sendLocalReply(const ThriftProxy::DirectResponse& response, bool end_stream) override {
    callbacks_->sendLocalReply(response, end_stream);
  }
  void onReset() override { callbacks_->onReset(); }

  // RequestOwner::ProtocolConverter
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus transportEnd() override;
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus messageEnd() override;
  FilterStatus passthroughData(Buffer::Instance& data) override;
  FilterStatus structBegin(absl::string_view name) override;
  FilterStatus structEnd() override;
  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override;
  FilterStatus fieldEnd() override;
  FilterStatus boolValue(bool& value) override;
  FilterStatus byteValue(uint8_t& value) override;
  FilterStatus int16Value(int16_t& value) override;
  FilterStatus int32Value(int32_t& value) override;
  FilterStatus int64Value(int64_t& value) override;
  FilterStatus doubleValue(double& value) override;
  FilterStatus stringValue(absl::string_view value) override;
  FilterStatus mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) override;
  FilterStatus mapEnd() override;
  FilterStatus listBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus listEnd() override;
  FilterStatus setBegin(FieldType& elem_type, uint32_t& size) override;
  FilterStatus setEnd() override;

  // Upstream::LoadBalancerContext
  const Network::Connection* downstreamConnection() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    const Envoy::Router::MetadataMatchCriteria* route_criteria =
        (route_entry_ != nullptr) ? route_entry_->metadataMatchCriteria() : nullptr;

    // Support getting metadata match criteria from thrift request.
    const auto& request_metadata = callbacks_->streamInfo().dynamicMetadata().filter_metadata();
    const auto filter_it = request_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);

    if (filter_it == request_metadata.end()) {
      return route_criteria;
    }

    if (route_criteria != nullptr) {
      metadata_match_criteria_ = route_criteria->mergeMatchCriteria(filter_it->second);
    } else {
      metadata_match_criteria_ =
          std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
    }

    return metadata_match_criteria_.get();
  }

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void cleanup();

  ThriftFilters::DecoderFilterCallbacks* callbacks_{};
  std::unique_ptr<UpstreamResponseCallbacksImpl> upstream_response_callbacks_{};
  RouteConstSharedPtr route_{};
  const RouteEntry* route_entry_{};
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;

  std::unique_ptr<UpstreamRequest> upstream_request_;
  Buffer::OwnedImpl upstream_request_buffer_;

  bool passthrough_supported_ : 1;
  uint64_t request_size_{};
  Runtime::Loader& runtime_;
  ShadowWriter& shadow_writer_;
  std::vector<std::reference_wrapper<ShadowRouterHandle>> shadow_routers_{};

  bool close_downstream_on_error_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
