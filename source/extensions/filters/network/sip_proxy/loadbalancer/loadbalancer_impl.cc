//#include "extensions/filters/common/ratelimit/ratelimit_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
//#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "extensions/filters/network/sip_proxy/loadbalancer/loadbalancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace LoadBalancer {

GrpcClientImpl::GrpcClientImpl(Grpc::RawAsyncClientPtr&& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout,
                               envoy::config::core::v3::ApiVersion transport_api_version)
    : async_client_(std::move(async_client)), timeout_(timeout),
      service_method_(Grpc::VersionedMethods(
                          "envoy.extensions.filters.network.sip_proxy.v3.TraService.nodes",
                          "envoy.extensions.filters.network.sip_proxy.v2.TraService.nodes")
                          .getMethodDescriptorForVersion(transport_api_version)),
      transport_api_version_(transport_api_version) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::createRequest(
    envoy::extensions::filters::network::sip_proxy::v3::TraRequest& request,
    const std::string& fqdn) {
  request.set_fqdn(fqdn);
}

void GrpcClientImpl::nodes(RequestCallbacks& callbacks, const std::string& fqdn,
                           Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  envoy::extensions::filters::network::sip_proxy::v3::TraRequest request;
  createRequest(request, fqdn);

  ENVOY_LOG(error, "XXXXXXXXXXXXXXXXXXXXXX GrpcClientImpl::nodes");
  request_ = async_client_->send(service_method_, request, *this, parent_span,
                                 Http::AsyncClient::RequestOptions().setTimeout(timeout_),
                                 transport_api_version_);
}

void GrpcClientImpl::onSuccess(
    std::unique_ptr<envoy::extensions::filters::network::sip_proxy::v3::TraResponse>&& response,
    Tracing::Span& span) {
  ENVOY_LOG(error, "XXXXXXXXXXXXXXXXXXXXXX GrpcClientImpl::onSuccess {}", response->nodes().size());
  for (auto & node : response->nodes()) {
    ENVOY_LOG(error, "{} {} {} {}", node.node_id(), node.ip(), node.sip_port(), node.weight());
  }

//  UNREFERENCED_PARAMETER(response);
  UNREFERENCED_PARAMETER(span);
  callbacks_->complete();
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ENVOY_LOG(error, "XXXXXXXXXXXXXXXXXXXXXX GrpcClientImpl::onFailure");
  ASSERT(status != Grpc::Status::WellKnownGrpcStatus::Ok);
  callbacks_ = nullptr;
}

ClientPtr traClient(Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                    const envoy::config::core::v3::GrpcService& grpc_service,
                    const std::chrono::milliseconds timeout,
                    envoy::config::core::v3::ApiVersion transport_api_version) {
  const auto async_client_factory =
      cluster_manager.grpcAsyncClientManager().factoryForGrpcService(grpc_service, scope, true);
  return std::make_unique<GrpcClientImpl>(async_client_factory->create(), timeout,
                                          transport_api_version);
}
} // namespace LoadBalancer
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
