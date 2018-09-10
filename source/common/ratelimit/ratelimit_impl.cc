#include "common/ratelimit/ratelimit_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/api/v2/ratelimit/ratelimit.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Envoy {
namespace RateLimit {

GrpcClientImpl::GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout,
                               const std::string& method_name)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name)),
      async_client_(std::move(async_client)), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::createRequest(envoy::service::ratelimit::v2::RateLimitRequest& request,
                                   const std::string& domain,
                                   const std::vector<Descriptor>& descriptors) {
  request.set_domain(domain);
  for (const Descriptor& descriptor : descriptors) {
    envoy::api::v2::ratelimit::RateLimitDescriptor* new_descriptor = request.add_descriptors();
    for (const DescriptorEntry& entry : descriptor.entries_) {
      envoy::api::v2::ratelimit::RateLimitDescriptor::Entry* new_entry =
          new_descriptor->add_entries();
      new_entry->set_key(entry.key_);
      new_entry->set_value(entry.value_);
    }
  }
}

void GrpcClientImpl::limit(RequestCallbacks& callbacks, const std::string& domain,
                           const std::vector<Descriptor>& descriptors, Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  envoy::service::ratelimit::v2::RateLimitRequest request;
  createRequest(request, domain, descriptors);

  request_ = async_client_->send(service_method_, request, *this, parent_span, timeout_);
}

void GrpcClientImpl::onSuccess(
    std::unique_ptr<envoy::service::ratelimit::v2::RateLimitResponse>&& response,
    Tracing::Span& span) {
  LimitStatus status = LimitStatus::OK;
  ASSERT(response->overall_code() != envoy::service::ratelimit::v2::RateLimitResponse_Code_UNKNOWN);
  if (response->overall_code() ==
      envoy::service::ratelimit::v2::RateLimitResponse_Code_OVER_LIMIT) {
    status = LimitStatus::OverLimit;
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOverLimit);
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
  }

  if (response->headers_size()) {
    Http::HeaderMapPtr headers = std::make_unique<Http::HeaderMapImpl>();
    for (const auto& h : response->headers()) {
      headers->addCopy(Http::LowerCaseString(h.key()), h.value());
    }
    callbacks_->complete(status, std::move(headers));
  } else {
    callbacks_->complete(status, nullptr);
  }

  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  callbacks_->complete(LimitStatus::Error, nullptr);
  callbacks_ = nullptr;
}

GrpcFactoryImpl::GrpcFactoryImpl(const envoy::config::ratelimit::v2::RateLimitServiceConfig& config,
                                 Grpc::AsyncClientManager& async_client_manager,
                                 Stats::Scope& scope)
    : use_data_plane_proto_(config.use_data_plane_proto()) {
  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.MergeFrom(config.grpc_service());
  // TODO(htuch): cluster_name is deprecated, remove after 1.6.0.
  if (config.service_specifier_case() ==
      envoy::config::ratelimit::v2::RateLimitServiceConfig::kClusterName) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(config.cluster_name());
  }
  async_client_factory_ = async_client_manager.factoryForGrpcService(grpc_service, scope, false);

  // TODO(junr03): legacy rate limit is deprecated. Remove this warning after 1.8.0.
  if (!use_data_plane_proto_) {
    // Force link time dependency on deprecated message type.
    pb::lyft::ratelimit::RateLimit _ignore;
    ENVOY_LOG_MISC(warn, "legacy rate limit client is deprecated, update your service to support "
                         "the data-plane-api defined rate limit service");
  }
}

ClientPtr GrpcFactoryImpl::create(const absl::optional<std::chrono::milliseconds>& timeout) {
  // TODO(junr03): legacy rate limit is deprecated. Remove support for the lyft proto after 1.8.0.
  if (use_data_plane_proto_) {
    return std::make_unique<GrpcClientImpl>(
        async_client_factory_->create(), timeout,
        "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit");
  }
  return std::make_unique<GrpcClientImpl>(async_client_factory_->create(), timeout,
                                          "pb.lyft.ratelimit.RateLimitService.ShouldRateLimit");
}

} // namespace RateLimit
} // namespace Envoy
