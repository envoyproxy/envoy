#include "common/ratelimit/ratelimit_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/headers.h"

#include "fmt/format.h"

namespace Envoy {
namespace RateLimit {

GrpcClientImpl::GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                               const Optional<std::chrono::milliseconds>& timeout)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "pb.lyft.ratelimit.RateLimitService.ShouldRateLimit")),
      async_client_(std::move(async_client)), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::createRequest(pb::lyft::ratelimit::RateLimitRequest& request,
                                   const std::string& domain,
                                   const std::vector<Descriptor>& descriptors) {
  request.set_domain(domain);
  for (const Descriptor& descriptor : descriptors) {
    pb::lyft::ratelimit::RateLimitDescriptor* new_descriptor = request.add_descriptors();
    for (const DescriptorEntry& entry : descriptor.entries_) {
      pb::lyft::ratelimit::RateLimitDescriptor::Entry* new_entry = new_descriptor->add_entries();
      new_entry->set_key(entry.key_);
      new_entry->set_value(entry.value_);
    }
  }
}

void GrpcClientImpl::limit(RequestCallbacks& callbacks, const std::string& domain,
                           const std::vector<Descriptor>& descriptors, Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  pb::lyft::ratelimit::RateLimitRequest request;
  createRequest(request, domain, descriptors);

  request_ = async_client_->send(service_method_, request, *this, parent_span, timeout_);
}

void GrpcClientImpl::onSuccess(std::unique_ptr<pb::lyft::ratelimit::RateLimitResponse>&& response,
                               Tracing::Span& span) {
  LimitStatus status = LimitStatus::OK;
  ASSERT(response->overall_code() != pb::lyft::ratelimit::RateLimitResponse_Code_UNKNOWN);
  if (response->overall_code() == pb::lyft::ratelimit::RateLimitResponse_Code_OVER_LIMIT) {
    status = LimitStatus::OverLimit;
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOverLimit);
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
  }

  callbacks_->complete(status);
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  UNREFERENCED_PARAMETER(status);
  callbacks_->complete(LimitStatus::Error);
  callbacks_ = nullptr;
}

GrpcFactoryImpl::GrpcFactoryImpl(const envoy::api::v2::RateLimitServiceConfig& config,
                                 Upstream::ClusterManager& cm)
    : cluster_name_(config.cluster_name()), cm_(cm) {
  if (!cm_.get(cluster_name_)) {
    throw EnvoyException(fmt::format("unknown rate limit service cluster '{}'", cluster_name_));
  }
}

ClientPtr GrpcFactoryImpl::create(const Optional<std::chrono::milliseconds>& timeout) {
  return std::make_unique<GrpcClientImpl>(
      std::make_unique<Grpc::AsyncClientImpl>(cm_, cluster_name_), timeout);
}

} // namespace RateLimit
} // namespace Envoy
