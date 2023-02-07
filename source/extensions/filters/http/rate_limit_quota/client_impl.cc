#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

// Helper function to build the usage report.
RateLimitQuotaUsageReports RateLimitClientImpl::buildUsageReport(absl::string_view domain,
                                                                 const BucketId& bucket_id) {
  // ASSERT(reports_ != nullptr);
  // TODO(tyxia) Domain is provided by filter configuration whose lifetime is tied with HCM. This
  // means that domain name has same lifetime as TLS because TLS is created by factory context. In
  // other words, there is only one domain name throughout the whole lifetime of `reports_`. (i.e.,
  // no need for container/map).
  if (reports_.domain().empty()) {
    // Set the domain name in the first report.
    reports_.set_domain(std::string(domain));
    // Add the usage report.
    auto* usage = reports_.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket_id;
    // TODO(tyxia) 1) Keep track of the time
    usage->mutable_time_elapsed()->set_seconds(0);
    // TODO(tyxia) How to set the value of requests allowed and denied.
    // This is requests allowed and denied for this bucket which contains many requests.
    // It seems that the lock is needed for this?? Because here is read from the map
    // update is write to map.
    usage->set_num_requests_allowed(1);
    usage->set_num_requests_denied(0);
  } else {
    RateLimitQuotaUsageReports& cached_report = reports_;
    bool updated = false;
    // TODO(tyxia) That will be better if it can be a map
    for (int idx = 0; idx < cached_report.bucket_quota_usages_size(); ++idx) {
      auto* usage = cached_report.mutable_bucket_quota_usages(idx);
      // Only update the quota usage for that specific bucket id.
      if (Protobuf::util::MessageDifferencer::Equals(usage->bucket_id(), bucket_id)) {
        usage->set_num_requests_allowed(usage->num_requests_allowed() + 1);
        // TODO(tyxia) How set the time elapsed.
        usage->mutable_time_elapsed()->set_seconds(1000);
        updated = true;
        // TODO(tyxia) Break the loop here since it should only one unique usage report for
        // particular bucket in the report.
        break;
      }
    }
    // New bucket in this report(i.e., No updates has been performed.)
    if (updated == false) {
      auto* usage = reports_.add_bucket_quota_usages();
      *usage->mutable_bucket_id() = bucket_id;
      usage->mutable_time_elapsed()->set_seconds(0);
      usage->set_num_requests_allowed(1);
    }
  }
  return reports_;
}

void RateLimitClientImpl::sendUsageReport(absl::string_view domain,
                                          absl::optional<BucketId> bucket_id) {
  ASSERT(stream_ != nullptr);
  // There is no bucked id provided in periodical sending behavior.
  stream_->sendMessage(bucket_id.has_value() ? buildUsageReport(domain, bucket_id.value())
                                             : reports_,
                       /*end_stream=*/true);
}

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&& response) {
  // ASSERT(callbacks_ != nullptr);
  // callbacks_->onQuotaResponse(*response);
  for (auto action : response->bucket_action()) {
    if (!action.has_bucket_id() || action.bucket_id().bucket().empty()) {
      ENVOY_LOG(error, "Received a Response whose bucket action is missing its bucket_id: ",
                response->ShortDebugString());
      continue;
    }
    // TODO(tyxia) Lifetime issue, here response is the reference but i need to store it in to
    // cache. So we need to pass by reference??? or build it here.
    // There is no update here!!!
    quota_buckets_[action.bucket_id()].bucket_action = BucketAction(action);
  }

  // TODO(tyxia) Keep this async callback interface here to do other post-processing.
  callbacks_.onQuotaResponse(*response);
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
  // TODO(tyxia) Add implementation later.
  stream_closed_ = true;
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
  return absl::OkStatus();
}

// TODO(tyxia) Remove??? sendUsageReport() did the work of rateLimit();
void RateLimitClientImpl::rateLimit(RateLimitQuotaCallbacks&) {
  // ASSERT(callbacks_ == nullptr);
  // callbacks_ = &callbacks;
  // ASSERT(stream_ != nullptr);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
