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
              (const BucketId& bucket_id, size_t id, const BucketAction& default_bucket_action,
               std::unique_ptr<envoy::type::v3::RateLimitStrategy> fallback_action,
               std::chrono::milliseconds fallback_ttl, bool initial_request_allowed),
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
        .Times(testing::AtLeast(1))
        .WillRepeatedly(Invoke(this, &RateLimitTestClient::mockCreateAsyncClient));
  }

  void failClientCreation() {
    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.async_client_manager_,
                getOrCreateRawAsyncClientWithHashKey(_, _, _))
        .Times(testing::AtLeast(1))
        .WillRepeatedly([]() { return absl::InternalError("Mock client creation failure"); });
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
    // The stream object is only directly reset when undergoing filter shutdown.
    EXPECT_CALL(stream_, resetStream());
  }

  // We don't know the eventual intent of each timer at creation time. Expect
  // the creation of a reporting timer and the expected number of expiration
  // timers, with type determined at enableTimer() time.
  void expectTimerCreations(
      std::chrono::milliseconds report_interval,
      std::chrono::milliseconds expiration_interval = std::chrono::milliseconds::zero(),
      int expiration_count = 0,
      std::chrono::milliseconds fallback_interval = std::chrono::milliseconds::zero(),
      int fallback_count = 0) {
    all_timers_ = std::vector<Event::MockTimer*>(1 + expiration_count + fallback_count);
    for (size_t i = 0; i < all_timers_.size(); ++i) {
      auto* timer_ptr = new Event::MockTimer(dispatcher_);
      all_timers_[i] = timer_ptr;
      // Prepare the timer to be either a reporting or expiration timer.
      EXPECT_CALL(*timer_ptr, enableTimer(_, _))
          .WillOnce([&, report_interval, expiration_interval, expiration_count, fallback_interval,
                     fallback_count, timer_ptr](std::chrono::milliseconds period, Unused) {
            if (period == report_interval) {
              // Only one reporting timer should be created.
              timer_ = timer_ptr;
              // Repeated enable calls to the reporting timer are expected with
              // each reporting cycle.
              EXPECT_CALL(*timer_ptr, enableTimer(report_interval, _))
                  .WillRepeatedly([&](Unused, Unused) { timer_->enabled_ = true; });
            } else if (period == expiration_interval && expiration_count > 0) {
              // Count expiration timers as they're enabled for the sake of test
              // verification.
              expiration_timers_.insert(timer_ptr);
            } else if (period == fallback_interval && fallback_count > 0) {
              // Count fallback timers as they're enabled for the sake of test
              // verification.
              fallback_timers_.insert(timer_ptr);
            } else {
              return;
            }
            timer_ptr->enabled_ = true;
          })
          .RetiresOnSaturation();
    }
  }

  inline static Event::MockTimer* assertMockTimer(Event::Timer* timer) {
    if (timer == nullptr)
      return nullptr;
    return dynamic_cast<Event::MockTimer*>(timer);
  }

  void expectTimeSource() {}

  Grpc::RawAsyncClientSharedPtr mockCreateAsyncClient(Unused, Unused, Unused) {
    if (async_client_ != nullptr) {
      return async_client_;
    }
    async_client_ = std::make_shared<Grpc::MockAsyncClient>();
    return async_client_;
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
  std::shared_ptr<Grpc::MockAsyncClient> async_client_ = nullptr;
  Grpc::MockAsyncStream stream_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  Grpc::Status::GrpcStatus grpc_status_ = Grpc::Status::WellKnownGrpcStatus::Ok;
  int fail_starts_ = 0;
  std::string domain_ = "cloud_12345_67890_rlqs";
  // Control timing by directly calling invokeCallback().
  Event::MockTimer* timer_ = nullptr;
  // Note: all expiration timers that are still running at the end of the test
  // will cause a failure, so tests must verify expiration behaviors.
  std::vector<Event::MockTimer*> all_timers_;
  absl::flat_hash_set<Event::MockTimer*> expiration_timers_ = {};
  absl::flat_hash_set<Event::MockTimer*> fallback_timers_ = {};
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
