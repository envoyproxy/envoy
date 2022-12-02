#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

// Helper function to build the usage report.
// TODO(tyxia) A list of quota usage reports
// https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/api/envoy/service/rate_limit_quota/v3/rlqs.proto;rcl=481403846;l=95
// because same bucket can have multiple reports?????!!!!!! but we report all the old reports
// repeatly as well???? but that means you need to save the RateLimitQuotaUsageReports

// RateLimitQuotaUsageReports contains reports for multiple buckets!!!!!!!
// so RateLimitQuotaUsageReports is not mapped to each bucket.
// Its key can be domain !!!!!
//

// So we should report usage for multiple buckets all at once????
// first rquest definitely just one bucket as that is the first request of the bucket
// 可能是: 1) filter 有一个member 是 RateLimitQuotaUsageReports
// 2) every time if the request matched and not first request, fill in the reports
// 3) periodically sent all at once
RateLimitQuotaUsageReports RateLimitClientImpl::buildUsageReport(absl::optional<BucketId> bucket_id) {
  RateLimitQuotaUsageReports reports;
  if (bucket_id.has_value() && bucket_usage_.find(bucket_id.value()) == bucket_usage_.end()) {
    auto bucket_id_val = bucket_id.value();
    // First request.
    // The domain should only be provided in the first report.
    std::string domain = "cloud_12345_67890_td_rlqs";
    reports.set_domain(domain);
    auto* usage = reports.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket_id_val;
    usage->mutable_time_elapsed()->set_seconds(0);
    bucket_usage_[bucket_id_val].domain = domain;
    bucket_usage_[bucket_id_val].idx = reports.bucket_quota_usages_size() - 1;
    bucket_usage_[bucket_id_val].usage = *usage;
  } else {
    // This case for both
    // 1) no bucket id ---- perodical send behavior.
    // 2) has bucket id and it is found in the cache, i.e., NOT the first request.
    for (auto it : bucket_usage_) {
      BucketId id = it.first;
      // This will update the `bucket_usage_` as well???
      std::string domain_name = bucket_usage_[id].domain;
      // 1. Update the `bucket_usage_`
      // TODO(tyxia) How to update the current usage!!!!
      bucket_usage_[id].usage.set_num_requests_allowed(
          bucket_usage_[id].usage.num_requests_allowed() + 1);
      bucket_usage_[id].usage.mutable_time_elapsed()->set_seconds(1000);

      // 2. Update the usage in the reports that will be sent to server.
      // Even though we have `bucket_usage_` stored, we still need to build the reports here from
      // scratch.
      auto* added_usage = reports.add_bucket_quota_usages();
      // CopyFrom will not overrride the existing entry but mergeFrom will do.
      added_usage->CopyFrom(bucket_usage_[bucket_id.value()].usage);
    }
  }
  return reports;
}

RateLimitQuotaUsageReports RateLimitClientImpl::buildUsageReport2(const BucketId& bucket_id) {
  RateLimitQuotaUsageReports reports;
  std::string domain = "cloud_12345_67890_td_rlqs";
  if (usage_reports_.find(domain) == usage_reports_.end()) {
    // First request.
    // The domain should only be provided in the first report.
    reports.set_domain(domain);
    auto* usage = reports.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket_id;
    usage->mutable_time_elapsed()->set_seconds(0);
    // Update the map
    usage_reports_[domain] = reports;
  } else {
    RateLimitQuotaUsageReports stored_report = usage_reports_[domain];
    // Update the stored report
    for (int idx = 0; idx < stored_report.bucket_quota_usages_size(); ++idx) {
      auto* usage = stored_report.mutable_bucket_quota_usages(idx);

      usage->set_num_requests_allowed(usage->num_requests_allowed() + 1);
      usage->mutable_time_elapsed()->set_seconds(1000);
    }
    reports.CopyFrom(stored_report);
  }

  // if (bucket_usage_.find(bucket_id) == bucket_usage_.end()) {
  //   // First request.
  //   // The domain should only be provided in the first report.
  //   std::string domain = "cloud_12345_67890_td_rlqs";
  //   reports.set_domain(domain);
  //   auto* usage = reports.add_bucket_quota_usages();
  //   *usage->mutable_bucket_id() = bucket_id;
  //   usage->mutable_time_elapsed()->set_seconds(0);
  //   // Update the map
  //   usage_reports_[domain] = reports;
  //   bucket_usage_[bucket_id].domain = domain;
  //   bucket_usage_[bucket_id].idx = reports.bucket_quota_usages_size() - 1;
  //   bucket_usage_[bucket_id].usage = *usage;
  // } else {
  //   for (auto it : bucket_usage_) {
  //     BucketId id = it.first;
  //     // This will update the `usage_reports_` and `bucket_usage_` as well???
  //     std::string domain_name = bucket_usage_[id].domain;
  //     // 1. Update the `bucket_usage_`
  //     bucket_usage_[id].usage.set_num_requests_allowed(
  //         bucket_usage_[id].usage.num_requests_allowed() + 1);
  //     bucket_usage_[id].usage.mutable_time_elapsed()->set_seconds(1000);
  //     // 2. Update `usage_reports_`
  //     auto* usage = usage_reports_[domain_name].mutable_bucket_quota_usages(bucket_usage_[id].idx);
  //     // CopyFrom will not overrride the existing entry but mergeFrom will do.
  //     usage->CopyFrom(bucket_usage_[bucket_id].usage);

  //     // 3. Update the usage in the reports that will be sent to server.
  //     // Even though we have `bucket_usage_` stored, we still need to build the reports here from
  //     // scratch.
  //     auto* added_usage = reports.add_bucket_quota_usages();
  //     added_usage->CopyFrom(bucket_usage_[bucket_id].usage);
  //     reports.CopyFrom(usage_reports_[domain_name]);
  //   }
  // }
  return reports;
}

// Helper function to build the usage report.
RateLimitQuotaUsageReports buildReports(const std::vector<BucketId>& bucket_ids) {
  RateLimitQuotaUsageReports reports;
  // The domain should only be provided in the first report.
  // TODO(tyxia) Retrieve the domain
  bool first_request = true;
  if (first_request) {
    reports.set_domain("cloud_12345_67890_td_rlqs");
  }

  // TODO(tyxia) Why a list of bucket quota usage.
  for (auto bucket_id : bucket_ids) {
    auto* usage = reports.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket_id;
    // The time elapsed field for the first request is set to 0.
    // TODO(tyxia) 1) Keep track of the time 2) differientate the first request.
    // Maybe a flag in fuction arg to differientate the first request.
    if (first_request) {
      // `num_requests_allowed` and `num_requests_denied ` are not configured for the first request.
      usage->mutable_time_elapsed()->set_seconds(0);
    } else {
      // TODO(tyxia) How to configure these values.
      // It seems that we need to keep track of them in the filter.
      usage->mutable_time_elapsed()->set_seconds(120);
      // TODO(tyxia) How to set the value of requests allowed and denied.
      // This is requests allowed and denied for this bucket which contains many requests.
      // It seems that the lock is needed for this?? Becasue here is read from the map
      // update is write to map.
      usage->set_num_requests_allowed(50);
      usage->set_num_requests_denied(35);
    }
  }
  return reports;
}

void RateLimitClientImpl::sendUsageReport(absl::optional<BucketId> bucket_id) {
  ASSERT(stream_ != nullptr);
  // TODO(tyxia) Build the report and handle end_stream later.
  // RateLimitQuotaUsageReports reports = buildReport(bucket_id);
  send(buildUsageReport(bucket_id), /*end_stream=*/true);
}

// void RateLimitClientImpl::sendUsageReport(const BucketId* bucket_id) {
//   ASSERT(stream_ != nullptr);
//   // TODO(tyxia) Build the report and handle end_stream later.
//   // RateLimitQuotaUsageReports reports = buildReport(bucket_id);
//   if (bucket_id != nullptr) {
//     send(buildUsageReport(*bucket_id), /*end_stream=*/true);
//   } else {
//   }
// }
// TODO(tyxia) Send the report periodically!!!
// 1) go/c++-concurrency#periodic_tasks OR envoy style
// 2) dispatcher and Timer
//    https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/http/health_check/health_check.cc;rcl=476782648;l=43
//    https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/envoy/event/dispatcher.h;rcl=476782648;l=133
// Or
//    https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/http/jwt_authn/jwks_async_fetcher.cc;rcl=476782648;l=31
// Optional funciton arg?? only first time need the bucket_id ???
// Later, peridiocally send should be retrieved from the map


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
      return absl::InternalError("Unable to establish the new stream");
    }
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
