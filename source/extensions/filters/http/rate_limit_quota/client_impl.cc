#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

void RateLimitClientImpl::addNewBucket(const BucketId& bucket_id) {
  QuotaUsage quota_usage;
  quota_usage.num_requests_allowed = 1;
  quota_usage.num_requests_denied = 0;
  quota_usage.last_report = std::chrono::duration_cast<std::chrono::nanoseconds>(
      time_source_.monotonicTime().time_since_epoch());

  // Create new bucket and store it into quota cache.
  std::unique_ptr<Bucket> new_bucket = std::make_unique<Bucket>();
  new_bucket->quota_usage = quota_usage;
  quota_buckets_[bucket_id] = std::move(new_bucket);
}

RateLimitQuotaUsageReports RateLimitClientImpl::getReport(bool new_bucket) {
  RateLimitQuotaUsageReports report;
  // Build the whole new report from quota_buckets_
  for (const auto& [id, bucket] : quota_buckets_) {
    auto* usage = report.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = id;
    usage->set_num_requests_allowed(bucket->quota_usage.num_requests_allowed);
    usage->set_num_requests_denied(bucket->quota_usage.num_requests_denied);
    if (new_bucket) {
      *usage->mutable_time_elapsed() = Protobuf::util::TimeUtil::NanosecondsToDuration(0);
    } else {
      auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
          time_source_.monotonicTime().time_since_epoch());
      *usage->mutable_time_elapsed() = Protobuf::util::TimeUtil::NanosecondsToDuration(
          (now - bucket->quota_usage.last_report).count());
    }
  }

  // Set the domain.
  report.set_domain(domain_name_);
  return report;
}

RateLimitQuotaUsageReports RateLimitClientImpl::buildUsageReport(const BucketId& bucket_id) {
  // ASSERT(quota_usage_reports_ != nullptr);
  // TODO(tyxia) Domain is provided by filter configuration whose lifetime is tied with HCM. This
  // means that domain name has same lifetime as TLS because TLS is created by factory context. In
  // other words, there is only one domain name throughout the whole lifetime of
  // `quota_usage_reports_`. (i.e., no need for container/map).

  RateLimitQuotaUsageReports report;
  bool new_bucket = false;

  if (quota_buckets_.find(bucket_id) != quota_buckets_.end()) {
    // Update the num_requests_allowed if bucket is found from the cache.
    quota_buckets_[bucket_id]->quota_usage.num_requests_allowed += 1;
  } else {
    // Add new bucket to the cache.
    addNewBucket(bucket_id);
    new_bucket = true;
  }

  // Get the report from quota bucket cache.
  report = getReport(new_bucket);
  return report;
}

void RateLimitClientImpl::sendUsageReport(absl::optional<BucketId> bucket_id) {
  ASSERT(stream_ != nullptr);
  // In periodical report case, there is no bucked id provided because client just reports
  // based on the cached report which is also updated every time we build the report (i.e.,
  // buildUsageReport is called).
  // TODO(tyxia) end_stream means send and close.
  stream_->sendMessage(bucket_id.has_value() ? buildUsageReport(bucket_id.value())
                                             : getReport(false),
                       /*end_stream=*/false);
}

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&& response) {
  for (const auto& action : response->bucket_action()) {
    if (!action.has_bucket_id() || action.bucket_id().bucket().empty()) {
      ENVOY_LOG(error,
                "Received a response, but bucket_id is missing : ", response->ShortDebugString());
      continue;
    }
    BucketId bucket_id = action.bucket_id();
    if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
      // TODO(tyxia) Allocate and extend lifetime to store in the bucket. We probably should not
      // create new bucket when response is not matched. The new bucket here doesn't have essential
      // elements like rate limiting client.

      // std::unique_ptr<Bucket> new_bucket = std::make_unique<Bucket>();
      // new_bucket->action = std::make_unique<BucketAction>(std::move(action));
      // quota_buckets_[bucket_id] = std::move(new_bucket);
      ENVOY_LOG(error,
                "Received a response, but but it is not matched any quota "
                "cache entry: ",
                response->ShortDebugString());
    } else {
      // quota_buckets_[bucket_id]->bucket_action = std::make_unique<BucketAction>(action);
      // TODO(tyxia) Reference lifetime extension.
      quota_buckets_[bucket_id]->bucket_action = action;
    }
  }
  // TODO(tyxia) Keep this async callback interface here to do other post-processing.
  // This doesn't work for periodical response as filter has been destroyed.
  if (rlqs_callback_ != nullptr) {
    rlqs_callback_->onQuotaResponse(*response);
  }
}
void RateLimitClientImpl::closeStream() {
  // Close the stream if it is in open state.
  if (stream_ != nullptr && !stream_closed_) {
    // TODO(tyxia) Google_grpc from onRemoteClose will call here because stream_ not null.
    // but stream_closed_ is true which prevent it.
    stream_->closeStream();
    stream_closed_ = true;
    stream_->resetStream();
  }
}

void RateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) {
  // TODO(tyxia) Add implementation later.
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
