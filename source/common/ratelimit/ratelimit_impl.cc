#include "common/ratelimit/ratelimit_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/tracing/context.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/headers.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace RateLimit {

GrpcClientImpl::GrpcClientImpl(RateLimitAsyncClientPtr&& async_client,
                               const Optional<std::chrono::milliseconds>& timeout)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "pb.lyft.ratelimit.RateLimitService.ShouldRateLimit")),
      async_client_(std::move(async_client)), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  ASSERT(stream_ != nullptr);
  stream_->resetStream();
  callbacks_ = nullptr;
  stream_ = nullptr;
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
                           const std::vector<Descriptor>& descriptors,
                           const Tracing::TransportContext& context) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;
  context_ = context;

  pb::lyft::ratelimit::RateLimitRequest request;
  createRequest(request, domain, descriptors);

  stream_ = async_client_->start(service_method_, *this, timeout_);
  if (stream_ == nullptr) {
    callbacks_->complete(LimitStatus::Error);
    callbacks_ = nullptr;
    return;
  }
  stream_->sendMessage(request);
}

void GrpcClientImpl::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  if (!context_.request_id_.empty()) {
    metadata.insertRequestId().value(context_.request_id_);
  }

  if (!context_.span_context_.empty()) {
    metadata.insertOtSpanContext().value(context_.span_context_);
  }
}

void GrpcClientImpl::onReceiveMessage(
    std::unique_ptr<pb::lyft::ratelimit::RateLimitResponse>&& message) {
  response_ = std::move(message);
}

void GrpcClientImpl::onRemoteClose(Grpc::Status::GrpcStatus status) {
  if (status != Grpc::Status::GrpcStatus::Ok) {
    callbacks_->complete(LimitStatus::Error);
    callbacks_ = nullptr;
    stream_ = nullptr;
    return;
  }

  LimitStatus limit_status = LimitStatus::OK;
  ASSERT(response_->overall_code() != pb::lyft::ratelimit::RateLimitResponse_Code_UNKNOWN);
  if (response_->overall_code() == pb::lyft::ratelimit::RateLimitResponse_Code_OVER_LIMIT) {
    limit_status = LimitStatus::OverLimit;
  }

  callbacks_->complete(limit_status);
  callbacks_ = nullptr;
  stream_->closeStream();
  stream_ = nullptr;
}

GrpcFactoryImpl::GrpcFactoryImpl(const Json::Object& config, Upstream::ClusterManager& cm)
    : cluster_name_(config.getString("cluster_name")), cm_(cm) {
  if (!cm_.get(cluster_name_)) {
    throw EnvoyException(fmt::format("unknown rate limit service cluster '{}'", cluster_name_));
  }
}

ClientPtr GrpcFactoryImpl::create(const Optional<std::chrono::milliseconds>& timeout) {
  return ClientPtr{new GrpcClientImpl(
      RateLimitAsyncClientPtr{
          new Grpc::AsyncClientImpl<pb::lyft::ratelimit::RateLimitRequest,
                                    pb::lyft::ratelimit::RateLimitResponse>(cm_, cluster_name_)},
      timeout)};
}

} // namespace RateLimit
} // namespace Envoy
