#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

void RateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) {
  stream_closed_ = true;
  // if (status == Grpc::Status::Ok) {
  //   ENVOY_LOG(debug, "gRPC stream closed remotely with OK status {}: {}", status, message);
  // } else {
  //   ENVOY_LOG(error, "gRPC stream closed remotely with error status {}: {}", status, message);
  // }
}

void RateLimitClientImpl::rateLimit(RateLimitQuotaCallbacks& callbacks) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;
  ASSERT(stream_ != nullptr);
  // TODO(tyxia) Build the report and handle end_stream later.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  send(std::move(reports), /*end_stream=*/true);
}

void RateLimitClientImpl::send(
    envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports&& reports, bool end_stream) {
  stream_->sendMessage(std::move(reports), end_stream);
}

absl::Status RateLimitClientImpl::startStream(const StreamInfo::StreamInfo& stream_info) {
  // Starts stream if it has not been opened yet.
  if (stream_ == nullptr) {
    stream_ = aync_client_.start(
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.rate_limit_quota.v3.RateLimitQuotaService.StreamRateLimitQuotas"),
        *this,
        Http::AsyncClient::RequestOptions().setParentContext(
            Http::AsyncClient::ParentContext{&stream_info}));

    if (stream_ == nullptr) {
      // std::string error_string = "Unable to establish the new stream";
      // ENVOY_LOG(error, error_string);
      return absl::InternalError("Unable to establish the new stream");
    }
  }
  return absl::OkStatus();
}

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&& response) {
  ASSERT(callbacks_ != nullptr);
  callbacks_->onReceive(response.get());
}

void RateLimitClientImpl::closeStream() {
  // Close the stream if it is in open state.
  if (stream_ != nullptr && !stream_closed_) {
    stream_->closeStream();
    stream_closed_ = true;
    stream_->resetStream();
  }
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
