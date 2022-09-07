#pragma once
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using GrpcAsyncClient =
    Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;
using RateLimitQuotaResponsePtr =
    std::unique_ptr<envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

// Grpc bi-directional stream client.
class RateLimitClientImpl : public RateLimitClient,
                            public Grpc::AsyncStreamCallbacks<
                                envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>,
                            // TODO(tyxia) Should I create a new log ID rate_limit for logging
                            public Logger::Loggable<Logger::Id::filter> {
public:
  RateLimitClientImpl(const envoy::config::core::v3::GrpcService& grpc_service,
                      Server::Configuration::FactoryContext& context)
      : aync_client_(context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
            grpc_service, context.scope(), true)) {
    // TODO(tyxia) Think about how to efficiently open grpc stream: whether it is opened on the
    // first request or creation of client.
    // startStream();
  }

  // TODO(tyxia) callbacks might not be needed to be implemented implemented as callback
  // code here
  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/common/ratelimit/ratelimit.h;rcl=466836161;l=89
  // needs callback is to provide the
  // AsyncStreamCallbacks
  void onReceiveMessage(RateLimitQuotaResponsePtr&&) override {}

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // RateLimitQuota::RateLimitClient
  void rateLimit() override;

  // TODO(tyxia) Do we need this to be abstract class in RateLimit?
  absl::Status startStream(const StreamInfo::StreamInfo& stream_info);
  void closeStream();
  void createReports(envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports&) {}
  void send(envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports&& reports,
            bool end_stream);

private:
  // Store the client as the bare object since there is no ownership transfer involved.
  GrpcAsyncClient aync_client_;
  Grpc::AsyncStream<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports> stream_;
  // TODO(tyxia) Further look at this flag
  bool stream_closed = false;
};

using RateLimitClientPtr = std::unique_ptr<RateLimitClientImpl>;

/**
 * Create the rate limit client. It is uniquely owned by each worker thread.
 */
inline RateLimitClientPtr
createRateLimitClient(Server::Configuration::FactoryContext& context,
                      const envoy::config::core::v3::GrpcService& grpc_service) {
  return std::make_unique<RateLimitClientImpl>(grpc_service, context);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
