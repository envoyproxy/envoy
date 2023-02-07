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
  // TODO(tyxia) Removed default nullptr arg.
  RateLimitClientImpl(const envoy::config::core::v3::GrpcService& grpc_service,
                      Server::Configuration::FactoryContext& context,
                      // TODO(tyxia) life time, filter itself destroyed but client is
                      // stored in the cached. need to outlived filter object!!
                      // TODO(tyxia) modified??? but why passed by reference???)
                      RateLimitQuotaCallbacks& callbacks, BucketsMap* const quota_buckets = nullptr,
                      RateLimitQuotaUsageReports* const usage_reports = nullptr)
      : aync_client_(context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
            grpc_service, context.scope(), true)),
        callbacks_(callbacks), quota_buckets_(quota_buckets), reports_(usage_reports) {}

  void onReceiveMessage(RateLimitQuotaResponsePtr&& response) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // RateLimitClient
  void rateLimit(RateLimitQuotaCallbacks& callbacks) override;

  absl::Status startStream(const StreamInfo::StreamInfo& stream_info);
  void closeStream();

  RateLimitQuotaUsageReports buildUsageReport(absl::string_view domain, const BucketId& bucket_id);
  void sendUsageReport(absl::string_view domain, absl::optional<BucketId> bucket_id);

private:
  // Store the client as the bare object since there is no ownership transfer involved.
  GrpcAsyncClient aync_client_;
  Grpc::AsyncStream<RateLimitQuotaUsageReports> stream_{};

  // TODO(tyxia) if the response is from periodical report, then the filter object is possible that
  // it is not the old filter anymore, how we do onQuotaResponse and update the treadLocal storage.
  // The TLS should be same but filter is different now how about storage it in the TLS then??? How
  // about the client and filter class have the pointer points to the same TLS object Then we don't
  // even need the callback to update it then!!!s
  RateLimitQuotaCallbacks& callbacks_;
  // TODO(tyxia) Further look at the use of this flag later.
  bool stream_closed_ = false;

  BucketsMap* const quota_buckets_ = nullptr;
  // The pointer to usage report object which is stored as thread local storage. i.e., It is
  // not stored in the filter here.
  RateLimitQuotaUsageReports* const reports_ = nullptr;
};

using RateLimitClientPtr = std::unique_ptr<RateLimitClientImpl>;
/**
 * Create the rate limit client. It is uniquely owned by each worker thread.
 */
inline RateLimitClientPtr createRateLimitClient(
    Server::Configuration::FactoryContext& context,
    const envoy::config::core::v3::GrpcService& grpc_service, RateLimitQuotaCallbacks& callbacks,
    RateLimitQuotaUsageReports* const quota_usage_reports, BucketsMap* const quota_buckets) {
  return std::make_unique<RateLimitClientImpl>(grpc_service, context, callbacks, quota_buckets,
                                               quota_usage_reports);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
