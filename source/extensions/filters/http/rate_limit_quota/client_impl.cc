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

// void RateLimitClientImpl::addNewBucket(const BucketId& bucket_id) {
//   QuotaUsage quota_usage;
//   quota_usage.num_requests_allowed = 1;
//   quota_usage.num_requests_denied = 0;
//   quota_usage.last_report = std::chrono::duration_cast<std::chrono::nanoseconds>(
//       time_source_.monotonicTime().time_since_epoch());

//   // Create new bucket and store it into quota cache.
//   std::unique_ptr<Bucket> new_bucket = std::make_unique<Bucket>();
//   new_bucket->quota_usage = quota_usage;
//   quota_buckets_[bucket_id] = std::move(new_bucket);
// }


// RateLimitQuotaUsageReports RateLimitClientImpl::buildUsageReport(const BucketId& bucket_id) {
//   bool new_bucket = false;

//   if (quota_buckets_.find(bucket_id) != quota_buckets_.end()) {
//     // Update the found entry in the quota cache.
//     // TODO(tyxia) Allow all requests for now, update it based on no assignment policy in next
//     // PRs.
//     quota_buckets_[bucket_id]->quota_usage.num_requests_allowed += 1;
//   } else {
//     // Add new bucket to the cache.
//     addNewBucket(bucket_id);
//     new_bucket = true;
//   }

//   // Get the report from quota bucket cache and return.
//   return buildReport(new_bucket);
// }

// void RateLimitClientImpl::sendUsageReport(absl::optional<BucketId> bucket_id) {
//   ASSERT(stream_ != nullptr);
//   // In periodical report case, there is no bucked id provided and client just reports
//   // the quota usage from cached report.
//   // TODO(tyxia) Revisit end_stream, means send and close.
//   stream_->sendMessage(bucket_id.has_value() ? buildUsageReport(bucket_id.value())
//                                              : buildReport(false),
//                        /*end_stream=*/false);
// }


void RateLimitClientImpl::sendUsageReport(absl::optional<size_t> bucket_id) {
  ASSERT(stream_ != nullptr);
  // In periodical report case, there is no bucked id provided and client just reports
  // the quota usage from cached report.
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
    // BucketId bucket_id = action.bucket_id();
    const size_t bucket_id = MessageUtil::hash(action.bucket_id());
    if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
      // The response should be matched to the report we sent.
      ENVOY_LOG(error,
                "Received a response, but but it is not matched any quota "
                "cache entry: ",
                response->ShortDebugString());
    } else {
      quota_buckets_[bucket_id]->bucket_action = action;
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
