#pragma once
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using GrpcAsyncClientPtr = std::unique_ptr<
    Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>>;

using RateLimitQuotaResponsePtr =
    std::unique_ptr<envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

// Grpc bi-directional stream client.
class RateLimitGrpcClientImpl : public RateLimitClient,
                                // TODO(tyxia) Why here is response ???
                                public Grpc::AsyncStreamCallbacks<
                                    envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>,
                                // TODO(tyxia) Should I create a new log ID rate_limit
                                public Logger::Loggable<Logger::Id::filter> {
public:
  // TODO(tyxia) Why need the timeout here
  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/common/ratelimit/ratelimit_impl.cc;rcl=399691660;l=22
  // TODO(tyxia) rvalue referecne
  RateLimitGrpcClientImpl(GrpcAsyncClientPtr&& client) { client_ = std::move(client); }
  // AsyncStreamCallbacks
  void onReceiveMessage(RateLimitQuotaResponsePtr&& message) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  void RateLimit() override {}

private:
  // TODO(tyxia) Use bare object or unique_ptr;
  GrpcAsyncClientPtr client_;
};

using RateLimitClientPtr = std::unique_ptr<RateLimitGrpcClientImpl>;

/**
 * Create the rate limit client.
 */
// TODO(tyxia) https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/common/ratelimit/ratelimit_impl.cc;rcl=399691660;l=125
// TODO(ramaraochavali): register client to singleton when GrpcClientImpl supports concurrent
// requests.
RateLimitClientPtr createRateLimitClient(Server::Configuration::FactoryContext& context,
                                         const envoy::config::core::v3::GrpcService& grpc_service) {
  // GrpcAsyncClientPtr grpcClient = std::make_unique<
  //     Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
  //                       envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>>(
  //     context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
  //         grpc_service, context.scope(), true, Grpc::CacheOption::CacheWhenRuntimeEnabled));

  // return std::make_unique<RateLimitGrpcClientImpl>(std::move(grpcClient));

  return std::make_unique<RateLimitGrpcClientImpl>(std::make_unique<
      Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                        envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>>(
      context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
          grpc_service, context.scope(), true, Grpc::CacheOption::CacheWhenRuntimeEnabled)));
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy