#pragma once
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::BucketId;
using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;
using BucketQuotaUsage =
    ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports::BucketQuotaUsage;
using GrpcAsyncClient =
    Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

// Grpc bidirectional streaming client which handles the communication with RLS server.
class RateLimitClientImpl : public RateLimitClient,
                            public Grpc::AsyncStreamCallbacks<
                                envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>,
                            public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  RateLimitClientImpl(const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                      Server::Configuration::FactoryContext& context, absl::string_view domain_name,
                      RateLimitQuotaCallbacks* callbacks, BucketsCache& quota_buckets);

  void onReceiveMessage(RateLimitQuotaResponsePtr&& response) override;

  // RawAsyncStreamCallbacks methods;
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // RateLimitClient methods.
  absl::Status startStream(const StreamInfo::StreamInfo* stream_info) override;
  void closeStream() override;
  // Send the usage report to RLQS server
  void sendUsageReport(absl::optional<size_t> bucket_id) override;
  void setCallback(RateLimitQuotaCallbacks* callbacks) override { rlqs_callback_ = callbacks; }
  // Notify the rate limit client that the filter itself has been destroyed. i.e., the filter
  // callback can not be used anymore.
  void resetCallback() override { rlqs_callback_ = nullptr; }

private:
  // Build the usage report (i.e., the request sent to RLQS server) from the buckets in quota bucket
  // cache.
  RateLimitQuotaUsageReports buildReport(absl::optional<size_t> bucket_id);
  // Domain from filter configuration. The same domain name throughout the whole lifetime of client.
  std::string domain_name_;
  // Client is stored as the bare object since there is no ownership transfer involved.
  GrpcAsyncClient aync_client_;
  Grpc::AsyncStream<RateLimitQuotaUsageReports> stream_{};
  // The callback that is used to communicate with filter.
  RateLimitQuotaCallbacks* rlqs_callback_ = nullptr;
  // Reference to quota bucket cache that is stored in TLS cache. It outlives the filter.
  BucketsCache& quota_buckets_;
  TimeSource& time_source_;
};

using RateLimitClientPtr = std::unique_ptr<RateLimitClientImpl>;
/**
 * Create the rate limit client. It is uniquely owned by each worker thread.
 */
inline RateLimitClientPtr
createRateLimitClient(Server::Configuration::FactoryContext& context,
                      RateLimitQuotaCallbacks* callbacks, BucketsCache& quota_buckets,
                      absl::string_view domain_name,
                      Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key) {
  return std::make_unique<RateLimitClientImpl>(config_with_hash_key, context, domain_name,
                                               callbacks, quota_buckets);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
