#include "ratelimit_impl.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/grpc/rpc_channel_impl.h"
#include "common/http/headers.h"

namespace RateLimit {

GrpcClientImpl::GrpcClientImpl(Grpc::RpcChannelFactory& factory,
                               const Optional<std::chrono::milliseconds>& timeout)
    : channel_(factory.create(*this, timeout)), service_(channel_.get()) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_);
  channel_->cancel();
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
                           const std::vector<Descriptor>& descriptors,
                           const std::string& request_id) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;
  request_id_ = request_id;

  pb::lyft::ratelimit::RateLimitRequest request;
  createRequest(request, domain, descriptors);
  service_.ShouldRateLimit(nullptr, &request, &response_, nullptr);
}

void GrpcClientImpl::onPreRequestCustomizeHeaders(Http::HeaderMap& headers) {
  if (request_id_ != EMPTY_STRING) {
    headers.addViaCopy(Http::Headers::get().RequestId, request_id_);
  }
}

void GrpcClientImpl::onSuccess() {
  LimitStatus status = LimitStatus::OK;
  ASSERT(response_.overall_code() != pb::lyft::ratelimit::RateLimitResponse_Code_UNKNOWN);
  if (response_.overall_code() == pb::lyft::ratelimit::RateLimitResponse_Code_OVER_LIMIT) {
    status = LimitStatus::OverLimit;
  }

  callbacks_->complete(status);
  callbacks_ = nullptr;
  request_id_.clear();
}

void GrpcClientImpl::onFailure(const Optional<uint64_t>&, const std::string&) {
  callbacks_->complete(LimitStatus::Error);
  callbacks_ = nullptr;
  request_id_.clear();
}

GrpcFactoryImpl::GrpcFactoryImpl(const Json::Object& config, Upstream::ClusterManager& cm,
                                 Stats::Store& stats_store)
    : cluster_name_(config.getString("cluster_name")), cm_(cm), stats_store_(stats_store) {
  if (!cm_.get(cluster_name_)) {
    throw EnvoyException(fmt::format("unknown rate limit service cluster '{}'", cluster_name_));
  }
}

ClientPtr GrpcFactoryImpl::create(const Optional<std::chrono::milliseconds>& timeout) {
  return ClientPtr{new GrpcClientImpl(*this, timeout)};
}

Grpc::RpcChannelPtr GrpcFactoryImpl::create(Grpc::RpcChannelCallbacks& callbacks,
                                            const Optional<std::chrono::milliseconds>& timeout) {
  return Grpc::RpcChannelPtr{
      new Grpc::RpcChannelImpl(cm_, cluster_name_, callbacks, stats_store_, timeout)};
}

} // RateLimit
