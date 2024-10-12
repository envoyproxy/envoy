#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

Grpc::RawAsyncClientSharedPtr
getOrThrow(absl::StatusOr<Grpc::RawAsyncClientSharedPtr> client_or_error) {
  THROW_IF_NOT_OK_REF(client_or_error.status());
  return client_or_error.value();
}

RateLimitClientImpl::RateLimitClientImpl(
    const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
    Server::Configuration::FactoryContext& context, absl::string_view domain_name,
    RateLimitQuotaCallbacks* callbacks, BucketsCache& quota_buckets)
    : domain_name_(domain_name),
      aync_client_(getOrThrow(
          context.serverFactoryContext()
              .clusterManager()
              .grpcAsyncClientManager()
              .getOrCreateRawAsyncClientWithHashKey(config_with_hash_key, context.scope(), true))),
      rlqs_callback_(callbacks), quota_buckets_(quota_buckets),
      time_source_(context.serverFactoryContext().mainThreadDispatcher().timeSource()) {}

RateLimitQuotaUsageReports RateLimitClientImpl::buildReport(absl::optional<size_t> bucket_id) {
  RateLimitQuotaUsageReports report;
  // Build the report from quota bucket cache.
  for (const auto& [id, bucket] : quota_buckets_) {
    auto* usage = report.add_bucket_quota_usages();
    *usage->mutable_bucket_id() = bucket->bucket_id;
    usage->set_num_requests_allowed(bucket->quota_usage.num_requests_allowed);
    usage->set_num_requests_denied(bucket->quota_usage.num_requests_denied);

    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        time_source_.monotonicTime().time_since_epoch());
    // For the newly created bucket (i.e., `bucket_id` input is not null), its time
    // elapsed since last report is 0.
    // This case happens when we send the report to RLQS server immediately.
    if (bucket_id.has_value() && bucket_id.value() == id) {
      *usage->mutable_time_elapsed() = Protobuf::util::TimeUtil::NanosecondsToDuration(0);
    } else {
      *usage->mutable_time_elapsed() = Protobuf::util::TimeUtil::NanosecondsToDuration(
          (now - bucket->quota_usage.last_report).count());
    }

    // Update the last_report time point.
    bucket->quota_usage.last_report = now;
    // Reset the number of request allowed/denied. The RLQS server expects the client to report
    // those two usage numbers only for last report period.
    bucket->quota_usage.num_requests_allowed = 0;
    bucket->quota_usage.num_requests_denied = 0;
  }

  // Set the domain name.
  report.set_domain(domain_name_);
  ENVOY_LOG(debug, "The usage report that will be sent to RLQS server:\n{}", report.DebugString());
  return report;
}

// This function covers both periodical report and immediate report case, with the difference that
// bucked id in periodical report case is empty.
void RateLimitClientImpl::sendUsageReport(absl::optional<size_t> bucket_id) {
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "The RLQS stream has been closed and must be restarted to send reports.");
    if (absl::Status err = startStream(nullptr); !err.ok()) {
      ENVOY_LOG(error, "Failed to start the stream to send reports.");
      return;
    }
  }

  // Build the report and then send the report to RLQS server.
  // `end_stream` should always be set to false as we don't want to close the stream locally.
  stream_->sendMessage(buildReport(bucket_id), /*end_stream=*/false);
}

void RateLimitClientImpl::onReceiveMessage(RateLimitQuotaResponsePtr&& response) {
  ENVOY_LOG(debug, "The response that is received from RLQS server:\n{}", response->DebugString());
  for (const auto& action : response->bucket_action()) {
    if (!action.has_bucket_id() || action.bucket_id().bucket().empty()) {
      ENVOY_LOG(error,
                "Received a response, but bucket_id is missing : ", response->ShortDebugString());
      continue;
    }

    // Get the hash id value from BucketId in the response.
    const size_t bucket_id = MessageUtil::hash(action.bucket_id());
    ENVOY_LOG(debug,
              "Received a response for bucket id proto :\n {}, and generated "
              "the associated hashed bucket id: {}",
              action.bucket_id().DebugString(), bucket_id);
    if (quota_buckets_.find(bucket_id) == quota_buckets_.end()) {
      // The response should be matched to the report we sent.
      ENVOY_LOG(error, "The received response is not matched to any quota cache entry: ",
                response->ShortDebugString());
    } else {
      switch (action.bucket_action_case()) {
      case envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse_BucketAction::
          kQuotaAssignmentAction: {
        absl::optional<BucketAction> cached_action = quota_buckets_[bucket_id]->cached_action;
        quota_buckets_[bucket_id]->current_assignment_time = time_source_.monotonicTime();

        if (cached_action.has_value() &&
            Protobuf::util::MessageDifferencer::Equals(*cached_action, action)) {
          ENVOY_LOG(debug,
                    "Cached action matches the incoming response so only TTL is updated for bucket "
                    "id: {}",
                    bucket_id);
          break;
        }
        quota_buckets_[bucket_id]->cached_action = action;
        if (quota_buckets_[bucket_id]->cached_action->has_quota_assignment_action()) {
          auto rate_limit_strategy = quota_buckets_[bucket_id]
                                         ->cached_action->quota_assignment_action()
                                         .rate_limit_strategy();

          if (rate_limit_strategy.has_token_bucket()) {
            const auto& interval_proto = rate_limit_strategy.token_bucket().fill_interval();
            // Convert absl::duration to int64_t seconds
            int64_t fill_interval_sec =
                absl::ToInt64Seconds(absl::Seconds(interval_proto.seconds()) +
                                     absl::Nanoseconds(interval_proto.nanos()));
            double fill_rate_per_sec =
                static_cast<double>(rate_limit_strategy.token_bucket().tokens_per_fill().value()) /
                fill_interval_sec;
            uint32_t max_tokens = rate_limit_strategy.token_bucket().max_tokens();
            ENVOY_LOG(trace,
                      "Created the token bucket limiter for hashed bucket "
                      "id: {}, with max_tokens: {}; "
                      "fill_interval_sec: {}; fill_rate_per_sec: {}.",
                      bucket_id, max_tokens, fill_interval_sec, fill_rate_per_sec);
            quota_buckets_[bucket_id]->token_bucket_limiter =
                std::make_unique<TokenBucketImpl>(max_tokens, time_source_, fill_rate_per_sec);
          }
        }
        break;
      }
      case envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse_BucketAction::
          kAbandonAction: {
        quota_buckets_.erase(bucket_id);
        ENVOY_LOG(debug, "Bucket id {} removed from the cache by abandon action.", bucket_id);
        break;
      }
      default: {
        ENVOY_LOG_EVERY_POW_2(error, "Unset bucket action type {}",
                              static_cast<int>(action.bucket_action_case()));
        break;
      }
      }
    }
    ENVOY_LOG(debug, "Assignment cached for bucket id {}.", bucket_id);
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
  if (stream_ != nullptr) {
    ENVOY_LOG(debug, "Closing gRPC stream");
    stream_->closeStream();
    stream_->resetStream();
    stream_ = nullptr;
  }
}

void RateLimitClientImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                        const std::string& message) {
  ENVOY_LOG(debug, "gRPC stream closed remotely with status {}: {}", status, message);
  stream_ = nullptr;
}

absl::Status RateLimitClientImpl::startStream(const StreamInfo::StreamInfo* stream_info) {
  // Starts stream if it has not been opened yet.
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "Trying to start the new gRPC stream");
    auto stream_options = Http::AsyncClient::RequestOptions();
    if (stream_info) {
      stream_options.setParentContext(Http::AsyncClient::ParentContext{stream_info});
    }
    stream_ = aync_client_.start(
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.rate_limit_quota.v3.RateLimitQuotaService.StreamRateLimitQuotas"),
        *this, stream_options);
  }

  // If still null after attempting a start.
  if (stream_ == nullptr) {
    return absl::InternalError("Failed to start the stream");
  }

  ENVOY_LOG(debug, "gRPC stream has been started");
  return absl::OkStatus();
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
