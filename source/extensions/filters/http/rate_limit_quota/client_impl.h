#pragma once
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

// TODO(tyxia) remove this alias if not needed.
using GrpcAsyncClientPtr = std::unique_ptr<
    Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>>;

using GrpcAsyncClient =
    Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
                      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

using RateLimitQuotaResponsePtr =
    std::unique_ptr<envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

// Grpc bi-directional stream client.
// TODO(tyxia) By current design, this class handles all the grpc operations.
class RateLimitClientImpl : public RateLimitClient,
                            // TODO(tyxia) Why here is response ???
                            public Grpc::AsyncStreamCallbacks<
                                envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>,
                            // TODO(tyxia) Should I create a new log ID rate_limit for logging
                            public Logger::Loggable<Logger::Id::filter> {
public:
  // TODO(tyxia) Why need the timeout here
  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/common/ratelimit/ratelimit_impl.cc;rcl=399691660;l=22
  // TODO(tyxia) rvalue reference is not recommended
  // go/cstyle#Rvalue_references
  // TODO(tyxia) Remove this function we can just create the async_client in place inside of
  // this class, i.e., no need to pass/move around.
  RateLimitClientImpl(GrpcAsyncClientPtr client) { client_ = std::move(client); }
  RateLimitClientImpl(Server::Configuration::FactoryContext& context,
                      const envoy::config::core::v3::GrpcService& grpc_service)
      : aync_client_(context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
            grpc_service, context.scope(), true, Grpc::CacheOption::CacheWhenRuntimeEnabled)) {

    // // TODO(tyxia) Think about how to efficiently open grpc stream
    // // How about starting the stream on the first request.
    // // The difference is when to create the stream object.
    // Http::AsyncClient::StreamOptions options;
    // stream_ = aync_client_.start(
    //     *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
    //         "envoy.service.rate_limit_quota.v3.RateLimitQuotaService.StreamRateLimitQuotas"),
    //     *this, options);
    // if (stream_ == nullptr) {
    //   ENVOY_LOG(warn, "Unable to establish new stream");
    //   // TODO(tyxia) Error handling
    // }
  }

  // TODO(tyxia) I don't see why callbacks need to be implemented implemented as callback
  // code here
  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/common/ratelimit/ratelimit.h;rcl=466836161;l=89
  // needs callback is to provide the
  // AsyncStreamCallbacks
  void onReceiveMessage(RateLimitQuotaResponsePtr&& message) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // RateLimitQuota::RateLimitClient
  void rateLimit() override;

  // TODO(tyxia) Do we need this to be abstract class in RateLimit
  void createReports(envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports& reports);
  // envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports buildBucketIds()
  bool startStream();
  void closeStream();

private:
  // TODO(tyxia) Use bare object or unique_ptr so far bare object seems works fine as it
  // should not require the ownership transfer.
  GrpcAsyncClientPtr client_;
  GrpcAsyncClient aync_client_;
  Grpc::AsyncStream<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports> stream_;
  // TODO(tyxia) Do we need this flag??
  bool stream_closed = false;
};

using RateLimitClientPtr = std::unique_ptr<RateLimitClientImpl>;

/**
 * Create the rate limit client.
 */
// TODO(tyxia)
// https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/extensions/filters/common/ratelimit/ratelimit_impl.cc;rcl=399691660;l=125
// TODO(ramaraochavali): register client to singleton when GrpcClientImpl supports concurrent
// requests.
inline RateLimitClientPtr
createRateLimitClient(Server::Configuration::FactoryContext& context,
                      const envoy::config::core::v3::GrpcService& grpc_service) {
  // return std::make_unique<RateLimitClientImpl>(
  //     std::make_unique<
  //         Grpc::AsyncClient<envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports,
  //                           envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>>(
  //         context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
  //             grpc_service, context.scope(), true, Grpc::CacheOption::CacheWhenRuntimeEnabled)));

  return std::make_unique<RateLimitClientImpl>(context, grpc_service);
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
