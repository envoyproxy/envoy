#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/async_client.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using Server::Configuration::MockFactoryContext;
using testing::_;
using testing::Invoke;
using testing::Return;
using testing::Unused;

// Used to mock a local rate limit client entirely.
class MockRateLimitClient : public RateLimitClient {
public:
  MockRateLimitClient() = default;
  ~MockRateLimitClient() override = default;

  MOCK_METHOD(void, createBucket,
              (const BucketId& bucket_id, size_t id, const BucketAction& initial_bucket_action,
               bool initial_request_allowed),
              (override));
  MOCK_METHOD(std::shared_ptr<CachedBucket>, getBucket, (size_t id), (override));
};

// Used when creating a "real" global rate limit client with mocked, underlying
// interfaces.
class RateLimitTestClient {
public:
  RateLimitTestClient() {
    grpc_service_.mutable_envoy_grpc()->set_cluster_name("rate_limit_quota");
    config_with_hash_key_ = Grpc::GrpcServiceConfigWithHashKey(grpc_service_);
  }

  void expectClientCreation() {
    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
                getOrCreateRawAsyncClientWithHashKey(_, _, _))
        .WillOnce(Invoke(this, &RateLimitTestClient::mockCreateAsyncClient));
  }

  void expectStreamCreation(int times) {
    if (times == 0) {
      EXPECT_CALL(*async_client_,
                  startRaw("envoy.service.rate_limit_quota.v3.RateLimitQuotaService",
                           "StreamRateLimitQuotas", _, _))
          .Times(0);
    } else if (times == 1) {
      EXPECT_CALL(*async_client_,
                  startRaw("envoy.service.rate_limit_quota.v3.RateLimitQuotaService",
                           "StreamRateLimitQuotas", _, _))
          .WillOnce(Invoke(this, &RateLimitTestClient::mockStartRaw));
    } else {
      EXPECT_CALL(*async_client_,
                  startRaw("envoy.service.rate_limit_quota.v3.RateLimitQuotaService",
                           "StreamRateLimitQuotas", _, _))
          .Times(times)
          .WillRepeatedly(Invoke(this, &RateLimitTestClient::mockStartRaw));
    }
  }

  void expectTimerCreation(std::chrono::milliseconds period) {
    timer_ = new Event::MockTimer(dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(period, _)).WillRepeatedly([&](Unused, Unused) {
      timer_->enabled_ = true;
    });
  }

  Grpc::RawAsyncClientSharedPtr mockCreateAsyncClient(Unused, Unused, Unused) {
    auto client = std::make_shared<Grpc::MockAsyncClient>();
    async_client_ = client.get();
    return client;
  }

  void setStreamStartToFail(int fail_starts) { fail_starts_ = fail_starts; }

  Grpc::RawAsyncStream* mockStartRaw(Unused, Unused, Grpc::RawAsyncStreamCallbacks& callbacks,
                                     const Http::AsyncClient::StreamOptions&) {
    if (fail_starts_ > 0) {
      fail_starts_--;
      return nullptr;
    }
    stream_callbacks_ = &callbacks;
    return &stream_;
  }

  ~RateLimitTestClient() = default;

  NiceMock<MockFactoryContext> context_;

  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  envoy::config::core::v3::GrpcService grpc_service_;
  Grpc::MockAsyncClient* async_client_ = nullptr;
  Grpc::MockAsyncStream stream_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  Grpc::Status::GrpcStatus grpc_status_ = Grpc::Status::WellKnownGrpcStatus::Ok;
  int fail_starts_ = 0;
  std::string domain_ = "cloud_12345_67890_rlqs";
  // Control timing by directly calling invokeCallback().
  Event::MockTimer* timer_ = nullptr;
  Event::MockDispatcher* dispatcher_ = &context_.server_factory_context_.dispatcher_;
};

class MockTokenBucket : public TokenBucket {
public:
  MockTokenBucket() = default;
  ~MockTokenBucket() override = default;

  MOCK_METHOD(uint64_t, consume, (uint64_t tokens, bool allow_partial), (override));
  MOCK_METHOD(uint64_t, consume,
              (uint64_t tokens, bool allow_partial, std::chrono::milliseconds& time_to_next_token),
              (override));
  MOCK_METHOD(std::chrono::milliseconds, nextTokenAvailable, (), (override));
  MOCK_METHOD(void, maybeReset, (uint64_t num_tokens), (override));
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
