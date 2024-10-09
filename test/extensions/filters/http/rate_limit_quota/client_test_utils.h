#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "test/extensions/filters/http/rate_limit_quota/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using Server::Configuration::MockFactoryContext;
using testing::Invoke;
using testing::Unused;

class RateLimitTestClient {
public:
  RateLimitTestClient() {
    // Initialize with own mock objects.
    grpc_service_.mutable_envoy_grpc()->set_cluster_name("rate_limit_quota");
    init(context_, grpc_service_);
  }

  RateLimitTestClient(bool start_failed) : start_failed_(start_failed) {
    // Initialize with own mock objects.
    grpc_service_.mutable_envoy_grpc()->set_cluster_name("rate_limit_quota");
    init(context_, grpc_service_);
  }

  RateLimitTestClient(NiceMock<MockFactoryContext>& context,
                      envoy::config::core::v3::GrpcService& grpc_service) {
    // Initialize with mock objects that are passed in externally.
    external_inited_ = true;
    init(context, grpc_service);
  }

  void init(NiceMock<MockFactoryContext>& context,
            envoy::config::core::v3::GrpcService& grpc_service) {
    // Set the expected behavior for async_client_manager in mock context.
    // Note, we need to set it through `MockFactoryContext` rather than `MockAsyncClientManager`
    // directly because the rate limit client object below requires context argument as the input.
    if (external_inited_) {
      EXPECT_CALL(context.server_factory_context_.cluster_manager_.async_client_manager_,
                  getOrCreateRawAsyncClient(_, _, _))
          .Times(2)
          .WillRepeatedly(Invoke(this, &RateLimitTestClient::mockCreateAsyncClient));
    } else {
      EXPECT_CALL(context.server_factory_context_.cluster_manager_.async_client_manager_,
                  getOrCreateRawAsyncClientWithHashKey(_, _, _))
          .WillOnce(Invoke(this, &RateLimitTestClient::mockCreateAsyncClient));
    }

    Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
        Grpc::GrpcServiceConfigWithHashKey(grpc_service);

    client_ =
        createRateLimitClient(context, &callbacks_, bucket_cache_, domain_, config_with_hash_key);
  }

  Grpc::RawAsyncClientSharedPtr mockCreateAsyncClient(Unused, Unused, Unused) {
    async_client_ = std::make_shared<Grpc::MockAsyncClient>();
    EXPECT_CALL(*async_client_, startRaw("envoy.service.rate_limit_quota.v3.RateLimitQuotaService",
                                         "StreamRateLimitQuotas", _, _))
        .WillRepeatedly(Invoke(this, &RateLimitTestClient::mockStartRaw));

    return async_client_;
  }

  Grpc::RawAsyncStream* mockStartRaw(Unused, Unused, Grpc::RawAsyncStreamCallbacks& callbacks,
                                     const Http::AsyncClient::StreamOptions&) {
    if (start_failed_) {
      return nullptr;
    }
    stream_callbacks_ = &callbacks;
    return &stream_;
  }

  ~RateLimitTestClient() = default;

  NiceMock<MockFactoryContext> context_;

  envoy::config::core::v3::GrpcService grpc_service_;
  Grpc::MockAsyncStream stream_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  Grpc::Status::GrpcStatus grpc_status_ = Grpc::Status::WellKnownGrpcStatus::Ok;
  RateLimitClientPtr client_;
  std::shared_ptr<Grpc::MockAsyncClient> async_client_ = nullptr;
  MockRateLimitQuotaCallbacks callbacks_;
  bool external_inited_ = false;
  bool start_failed_ = false;
  BucketsCache bucket_cache_;
  std::string domain_ = "cloud_12345_67890_rlqs";
};

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
