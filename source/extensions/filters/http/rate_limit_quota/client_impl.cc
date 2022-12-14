#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

// TODO(tyxia) Remove
// RateLimitQuotaUsageReports contains reports for multiple buckets!!!!!!!
// so RateLimitQuotaUsageReports is not mapped to each bucket.
// RateLimitQuotaUsageReports
// RateLimitClientImpl::buildUsageReportBucketUsage(absl::optional<BucketId> bucket_id) {
//   RateLimitQuotaUsageReports reports;
//   if (bucket_id.has_value() && bucket_usage_.find(bucket_id.value()) == bucket_usage_.end()) {
//     auto bucket_id_val = bucket_id.value();
//     // First request.
//     // The domain should only be provided in the first report.
//     std::string domain = "cloud_12345_67890_TD_rlqs";
//     reports.set_domain(domain);
//     auto* usage = reports.add_bucket_quota_usages();
//     *usage->mutable_bucket_id() = bucket_id_val;
//     usage->mutable_time_elapsed()->set_seconds(0);
//     bucket_usage_[bucket_id_val].domain = domain;
//     bucket_usage_[bucket_id_val].idx = reports.bucket_quota_usages_size() - 1;
//     bucket_usage_[bucket_id_val].usage = *usage;
//   } else {
//     // This case for both
//     // 1) no bucket id ---- periodical send behavior.
//     // 2) has bucket id and it is found in the cache, i.e., NOT the first request.
//     for (auto it : bucket_usage_) {
//       BucketId id = it.first;

//       // case 2), has the bucket id and the corresponding entry is found in the cache
//       // Update the `bucket_usage_`
//       // TODO(tyxia) How to update the current usage!!!!
//       // if (bucket_id.has_value() && id == bucket_id.value()) {
//       if (bucket_id.has_value() &&
//           Protobuf::util::MessageDifference::Equals(id, bucket_id.value())) {
//         bucket_usage_[id].usage.set_num_requests_allowed(
//             bucket_usage_[id].usage.num_requests_allowed() + 1);
//         bucket_usage_[id].usage.mutable_time_elapsed()->set_seconds(1000);
//       }

//       // 2. Always update the usage in the reports that will be sent to server.
//       // no matter if it is periodical send behavior or decode header place where the incoming
//       // request has arrived
//       // Even though we have `bucket_usage_` stored, we still need to build the reports here from
//       // scratch.
//       auto* added_usage = reports.add_bucket_quota_usages();
//       // CopyFrom will not override the existing entry but mergeFrom will do.
//       added_usage->CopyFrom(bucket_usage_[bucket_id.value()].usage);
//     }
//   }
//   return reports;
// }

// Helper function to build the usage report.
RateLimitQuotaUsageReports
RateLimitClientImpl::buildUsageReport(absl::string_view domain,
                                      absl::optional<BucketId> bucket_id) {
  if (reports_->domain().empty()) {
    // First report
    // The domain should only be set in the report for the first time. This domain is from the
    // filter configuration which should be available at the beginning
    // when filter is created.
    reports_->set_domain(std::string(domain));
    auto* usage = reports_->add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket_id.value();
    // TODO(tyxia) 1) Keep track of the time
    usage->mutable_time_elapsed()->set_seconds(0);
    // TODO(tyxia) How to set the value of requests allowed and denied.
    // This is requests allowed and denied for this bucket which contains many requests.
    // It seems that the lock is needed for this?? Because here is read from the map
    // update is write to map.
    usage->set_num_requests_allowed(1);
    usage->set_num_requests_denied(0);
  } else {
    // TODO(tyxia) use const pointer const to address
    // Use reference variable to update the reports_.
    RateLimitQuotaUsageReports& cached_report = *reports_;
    if (bucket_id.has_value()) {
      bool updated = false;
      // TODO(tyxia) That will be better if it can be a map
      for (int idx = 0; idx < cached_report.bucket_quota_usages_size(); ++idx) {
        auto* usage = cached_report.mutable_bucket_quota_usages(idx);
        // Only update the bucket with that specific id.
        if (bucket_id.has_value() &&
            Protobuf::util::MessageDifferencer::Equals(usage->bucket_id(), bucket_id.value())) {
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
        auto* usage = reports_->add_bucket_quota_usages();
        *usage->mutable_bucket_id() = bucket_id.value();
        usage->mutable_time_elapsed()->set_seconds(0);
        usage->set_num_requests_allowed(1);
      }
    }
  }
  return *reports_;
}

void RateLimitClientImpl::sendUsageReport(absl::string_view domain,
                                          absl::optional<BucketId> bucket_id) {
  ASSERT(stream_ != nullptr);
  stream_->sendMessage(buildUsageReport(domain, bucket_id), /*end_stream=*/true);
}

// TODO(tyxia) Remove
void RateLimitClientImpl::send(
    envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports&& reports, bool end_stream) {
  stream_->sendMessage(std::move(reports), end_stream);
}

void RateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) {
  // TODO(tyxia) Add implementation later.
  stream_closed_ = true;
}

void RateLimitClientImpl::rateLimit(RateLimitQuotaCallbacks& callbacks) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;
  ASSERT(stream_ != nullptr);
  // TODO(tyxia) Build the report and handle end_stream later.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  send(std::move(reports), /*end_stream=*/true);
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

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&& response) {
  ASSERT(callbacks_ != nullptr);
  callbacks_->onQuotaResponse(*response);
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
