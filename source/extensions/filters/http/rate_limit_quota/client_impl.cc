#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

RateLimitQuotaUsageReports RateLimitClientImpl::buildUsageReport(absl::string_view domain,
                                                                 const BucketId& bucket_id) {
  // ASSERT(quota_usage_reports_ != nullptr);
  // TODO(tyxia) Domain is provided by filter configuration whose lifetime is tied with HCM. This
  // means that domain name has same lifetime as TLS because TLS is created by factory context. In
  // other words, there is only one domain name throughout the whole lifetime of
  // `quota_usage_reports_`. (i.e., no need for container/map).
  if (quota_usage_reports_.domain().empty()) {
    // Set the domain name in the first report.
    quota_usage_reports_.set_domain(std::string(domain));
    // Add the usage report.
    auto* usage = quota_usage_reports_.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket_id;
    // TODO(tyxia) 1) Keep track of the time
    // It is better to be last report time and current time.
    usage->mutable_time_elapsed()->set_seconds(0);
    // TODO(tyxia) 2) Set the value of requests allowed and denied.
    // This is requests allowed and denied for this bucket which contains many requests.
    // It seems that the lock is needed for this?? Because here is read from the map
    // update is write to map.
    usage->set_num_requests_allowed(1);
    usage->set_num_requests_denied(0);
  }
  // If the cached report exists.
  else {
    RateLimitQuotaUsageReports& cached_report = quota_usage_reports_;
    bool updated = false;
    // TODO(tyxia) That will be better if it can be a map
    for (int idx = 0; idx < cached_report.bucket_quota_usages_size(); ++idx) {
      auto* usage = cached_report.mutable_bucket_quota_usages(idx);
      // Only update the quota usage for that specific bucket id.
      if (Protobuf::util::MessageDifferencer::Equals(usage->bucket_id(), bucket_id)) {
        usage->set_num_requests_allowed(usage->num_requests_allowed() + 1);
        // TODO(tyxia) Update the logic of setting the second logic.
        usage->mutable_time_elapsed()->set_seconds(1000);
        updated = true;
        // Break the loop here since it should only one unique usage report for
        // particular bucket in the report.
        break;
      }
    }
    // New bucket in this report(i.e., No updates has been performed.)
    if (updated == false) {
      auto* usage = quota_usage_reports_.add_bucket_quota_usages();
      *usage->mutable_bucket_id() = bucket_id;
      usage->mutable_time_elapsed()->set_seconds(0);
      usage->set_num_requests_allowed(1);
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
  if (rlqs_callbacks_ != nullptr) {
    rlqs_callbacks_->onQuotaResponse(*response);
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
