#pragma once
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket.h"

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
  RateLimitClientImpl(const envoy::config::core::v3::GrpcService& grpc_service,
                      Server::Configuration::FactoryContext& context,
                      RateLimitQuotaUsageReports* quota_usage_reports = nullptr)
      : aync_client_(context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
            grpc_service, context.scope(), true)),
        reports_(quota_usage_reports) {}

  void onReceiveMessage(RateLimitQuotaResponsePtr&& response) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // RateLimitClient
  void rateLimit(RateLimitQuotaCallbacks& callbacks) override;

  // TODO(tyxia) This function get rid of `bucket_usage_`
  // RateLimitQuotaUsageReports buildUsageReport2(absl::string_view domain,
  //                                              absl::optional<BucketId> bucket_id);
  // void sendUsageReport(const BucketId* bucket_id);
  void sendUsageReport(absl::string_view domain, absl::optional<BucketId> bucket_id);

  absl::Status startStream(const StreamInfo::StreamInfo& stream_info);
  void closeStream();
  void send(envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports&& reports,
            bool end_stream);
  RateLimitQuotaUsageReports buildUsageReport(absl::string_view domain,
                                              absl::optional<BucketId> bucket_id);
private:
  //RateLimitQuotaUsageReports buildUsageReportBucketUsage(absl::optional<BucketId> bucket_id);

  // Store the client as the bare object since there is no ownership transfer involved.
  GrpcAsyncClient aync_client_;
  Grpc::AsyncStream<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports> stream_{};
  RateLimitQuotaCallbacks* callbacks_{};
  // TODO(tyxia) Further look at the use of this flag later.
  bool stream_closed_ = false;

  // TODO(tyxia) Store it outside of filter, as thread local storage!!!!
  // All the struct below should be removed!!!!!
  struct BucketQuotaUsageInfo {
    BucketQuotaUsage usage;
    std::string domain;
    // The index
    int idx;
  };
  // 1. store <BucketId, BucketQuotaUsage>
  struct BucketIdHash {
    size_t operator()(const BucketId& bucket_id) const { return MessageUtil::hash(bucket_id); }
  };

  struct BucketIdEqual {
    bool operator()(const BucketId& id1, const BucketId& id2) const {
      return Protobuf::util::MessageDifferencer::Equals(id1, id2);
    }
  };
  absl::node_hash_map<BucketId, BucketQuotaUsageInfo, BucketIdHash, BucketIdEqual> bucket_usage_;
  // 2. store <domain, RateLimitQuotaUsageReports> No need for domain key
  // I am now thing useage_reports is better as long as the domain can be used to represent
  //   1. don't need to build the report from scratch everytime
  //   2. key is smaller
  // TODO(tyxia) const!!!!

  // Domain is provided by filter config that is tied with HCM lifetime which has same life time as
  // tls (because tls is created by factory context). So the domain will stay same throughout the
  // life time of the tls. (i.e., one domain per tls). So we don't need to have map/container here.
  RateLimitQuotaUsageReports* reports_ = nullptr;
};

using RateLimitClientPtr = std::unique_ptr<RateLimitClientImpl>;
using RateLimitClientSharedPtr = std::shared_ptr<RateLimitClientImpl>;
/**
 * Create the rate limit client. It is uniquely owned by each worker thread.
 */
inline RateLimitClientPtr
createRateLimitClient(Server::Configuration::FactoryContext& context,
                      const envoy::config::core::v3::GrpcService& grpc_service,
                      RateLimitQuotaUsageReports* quota_usage_reports) {
  return std::make_unique<RateLimitClientImpl>(grpc_service, context, quota_usage_reports);
}

inline RateLimitClientSharedPtr
createRateLimitGrpcClient(Server::Configuration::FactoryContext& context,
                          const envoy::config::core::v3::GrpcService& grpc_service) {
  return std::make_shared<RateLimitClientImpl>(grpc_service, context);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
