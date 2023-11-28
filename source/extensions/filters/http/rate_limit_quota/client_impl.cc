#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

RateLimitQuotaUsageReports RateLimitClientImpl::buildReport(absl::optional<size_t> bucket_id) {
  RateLimitQuotaUsageReports report;
  // Build the report from quota bucket cache.
  for (const auto& [id, bucket] : quota_buckets_) {
    auto* usage = report.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket->bucket_id;
    usage->set_num_requests_allowed(bucket->quota_usage.num_requests_allowed);
    usage->set_num_requests_denied(bucket->quota_usage.num_requests_denied);
    // For the newly created bucket (i.e., `bucket_id` input is not null), its time
    // elapsed since last report is 0.
    // This case happens when we send the report to RLQS server immediately.
    if (bucket_id.has_value() && bucket_id.value() == id) {
      *usage->mutable_time_elapsed() = Protobuf::util::TimeUtil::NanosecondsToDuration(0);
    } else {
      auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
          time_source_.monotonicTime().time_since_epoch());
      *usage->mutable_time_elapsed() = Protobuf::util::TimeUtil::NanosecondsToDuration(
          (now - bucket->quota_usage.last_report).count());
    }
  }

  // Set the domain name.
  report.set_domain(domain_name_);
  return report;
}

// This function covers both periodical report and immediate report case, with the difference that
// bucked id in periodical report case is empty.
void RateLimitClientImpl::sendUsageReport(absl::optional<size_t> bucket_id) {
  ASSERT(stream_ != nullptr);
  // Build the report and then send the report to RLQS server.
  // TODO(tyxia) Revisit end_stream, means send and close.
  stream_->sendMessage(buildReport(bucket_id), /*end_stream=*/false);
}

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&& response) {
  for (const auto& action : response->bucket_action()) {
    if (!action.has_bucket_id() || action.bucket_id().bucket().empty()) {
      ENVOY_LOG(error,
                "Received a response, but bucket_id is missing : ", response->ShortDebugString());
      continue;
    }

    // Get the hash id value from BucketId in the response.
    const size_t bucket_id = MessageUtil::hash(action.bucket_id());
    if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
      // The response should be matched to the report we sent.
      ENVOY_LOG(error,
                "Received a response, but but it is not matched any quota "
                "cache entry: ",
                response->ShortDebugString());
    } else {
      quota_buckets_[bucket_id]->bucket_action = action;
      // TODO(tyxia) Handle expired assignment via `assignment_time_to_live`.
      if (quota_buckets_[bucket_id]->bucket_action.has_quota_assignment_action()) {
        auto rate_limit_strategy = quota_buckets_[bucket_id]
                                       ->bucket_action.quota_assignment_action()
                                       .rate_limit_strategy();

        if (rate_limit_strategy.has_token_bucket()) {
          const auto& interval_proto = rate_limit_strategy.token_bucket().fill_interval();
          // Convert absl::duration to int64_t seconds
          int64_t fill_interval_sec = absl::ToInt64Seconds(
              absl::Seconds(interval_proto.seconds()) + absl::Nanoseconds(interval_proto.nanos()));
          double fill_rate_per_sec =
              static_cast<double>(rate_limit_strategy.token_bucket().tokens_per_fill().value()) /
              fill_interval_sec;

          quota_buckets_[bucket_id]->token_bucket_limiter = std::make_unique<TokenBucketImpl>(
              rate_limit_strategy.token_bucket().max_tokens(), time_source_, fill_rate_per_sec);
        }
      }
    }
  }

  // `rlqs_callback_` has been reset to nullptr for periodical report case.
  // No need to invoke onQuotaResponse to continue the filter chain for this case as filter chain
  // has not been paused.
  if (rlqs_callback_ != nullptr) {
    rlqs_callback_->onQuotaResponse(*response);
  }
}

void RateLimitClientImpl::closeStream() {
  // Close the stream if it is in open state.
  if (stream_ != nullptr && !stream_closed_) {
    stream_->closeStream();
    stream_closed_ = true;
    stream_->resetStream();
  }
}

void RateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) {
  // TODO(tyxia) Revisit later, maybe add some logging.
  stream_closed_ = true;
  closeStream();
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
  }

  // Returns error status if start failed (i.e., stream_ is nullptr).
  return stream_ == nullptr ? absl::InternalError("Failed to start the stream") : absl::OkStatus();
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
