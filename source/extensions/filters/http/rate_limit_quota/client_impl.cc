#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&&) {}

void RateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                        const std::string& message) {
  if (status == Grpc::Status::Ok) {
    ENVOY_LOG(debug, "gRPC stream closed remotely with OK status {}: {}", status, message);
  } else {
    ENVOY_LOG(error, "gRPC stream closed remotely with error status {}: {}", status, message);
  }
}

void RateLimitClientImpl::rateLimit() {
  ASSERT(stream_ != nullptr);
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  // TODO(tyxia) This could be wrapped into a utility function.
  stream_.sendMessage(std::move(reports), true);
}

bool RateLimitClientImpl::startStream() {
  // Starts stream if it has not been opened yet.
  if (stream_ == nullptr) {
    Http::AsyncClient::StreamOptions options;
    stream_ = aync_client_.start(
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.rate_limit_quota.v3.RateLimitQuotaService.StreamRateLimitQuotas"),
        *this, options);
    if (stream_ == nullptr) {
      ENVOY_LOG(error, "Unable to establish the new stream");
      return false;
      // TODO(tyxia) Error handling
      // re-try or other kinds of error handling actions
    }
  }
  return true;
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
