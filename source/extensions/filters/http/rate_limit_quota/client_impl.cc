#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

BucketQuotaUsage RateLimitClientImpl::addNewBucketUsage(const BucketId& bucket_id) {
  // Add the usage report.
  BucketQuotaUsage usage;
  *usage.mutable_bucket_id() = bucket_id;
  // Keep track of the time
  usage.mutable_time_elapsed()->set_seconds(0);
  // TimestampUtil::systemClockToTimestamp(time_source_.systemTime(),
  // *usage.mutable_last_report());

  usage.mutable_last_report()->MergeFrom(Protobuf::util::TimeUtil::MillisecondsToTimestamp(
      time_source_.monotonicTime().time_since_epoch().count()));
  // TODO(tyxia) 2) Set the value of requests allowed and denied.
  // This is requests allowed and denied for this bucket which contains many requests.
  // It seems that the lock is needed for this?? Because here is read from the map
  // update is write to map.
  usage.set_num_requests_allowed(1);
  usage.set_num_requests_denied(0);
  return usage;
}

RateLimitQuotaUsageReports RateLimitClientImpl::buildUsageReport(absl::string_view domain,
                                                                 const BucketId& bucket_id) {
  const uint64_t id = MessageUtil::hash(bucket_id);
  // TODO(tyxia) Domain is provided by filter configuration whose lifetime is tied with HCM. This
  // means that domain name has same lifetime as TLS because TLS is created by factory context. In
  // other words, there is only one domain name throughout the whole lifetime of
  // `quota_usage_reports_`. (i.e., no need for container/map).

  // First report.
  if (quota_usage_reports_.domain().empty()) {
    // Set the domain name in the first report.
    quota_usage_reports_.set_domain(std::string(domain));
    // Add the usage report.
    (*quota_usage_reports_.mutable_bucket_quota_usages())[id] = addNewBucketUsage(bucket_id);
  } else {
    auto quota_usage = quota_usage_reports_.bucket_quota_usages();
    if (quota_usage.find(id) != quota_usage.end()) {
      auto mutable_quota_usage = quota_usage_reports_.mutable_bucket_quota_usages();
      (*mutable_quota_usage)[id].set_num_requests_allowed(quota_usage[id].num_requests_allowed() +
                                                          1);
      // TODO(tyxia) Closer look
      auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     time_source_.monotonicTime().time_since_epoch())
                     .count();
      auto time_elapsed =
          now - Protobuf::util::TimeUtil::TimestampToNanoseconds(quota_usage[id].last_report());
      *(*mutable_quota_usage)[id].mutable_time_elapsed() =
          Protobuf::util::TimeUtil::NanosecondsToDuration(time_elapsed);
    } else {
      // New bucket in this report(i.e., No updates has been performed.)
      (*quota_usage_reports_.mutable_bucket_quota_usages())[id] = addNewBucketUsage(bucket_id);
    }
  }
  return quota_usage_reports_;
}

void RateLimitClientImpl::sendUsageReport(absl::string_view domain,
                                          absl::optional<BucketId> bucket_id) {
  ASSERT(stream_ != nullptr);
  // In periodical report case, there is no bucked id provided because client just reports
  // based on the cached report which is also updated every time we build the report (i.e.,
  // buildUsageReport is called).
  // TODO(tyxia) end_stream means send and close.
  stream_->sendMessage(bucket_id.has_value() ? buildUsageReport(domain, bucket_id.value())
                                             : quota_usage_reports_,
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
      // quota_buckets_[bucket_id]->bucket_action =
      //     std::make_unique<BucketAction>(std::move(action));
      quota_buckets_[bucket_id]->bucket_action = std::make_unique<BucketAction>(action);
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
