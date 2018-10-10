#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/stats/scope.h"

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/ratelimit/ratelimit_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::WithArg;

namespace Envoy {
namespace RateLimit {

class MockRequestCallbacks : public RequestCallbacks {
public:
  void complete(LimitStatus status, Http::HeaderMapPtr&& headers) {
    complete_(status, headers.get());
  }

  MOCK_METHOD2(complete_, void(LimitStatus status, const Http::HeaderMap* headers));
};

// TODO(junr03): legacy rate limit is deprecated. Remove the boolean parameter after 1.8.0.
class RateLimitGrpcClientTest : public testing::TestWithParam<bool> {
public:
  RateLimitGrpcClientTest() : async_client_(new Grpc::MockAsyncClient()) {}

  void setClient(const bool use_data_plane_proto) {
    if (use_data_plane_proto) {
      client_.reset(new GrpcClientImpl(
          Grpc::AsyncClientPtr{async_client_}, absl::optional<std::chrono::milliseconds>(),
          "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit"));
    } else {
      // Force link time dependency on deprecated message type.
      pb::lyft::ratelimit::RateLimit _ignore;
      client_.reset(new GrpcClientImpl(Grpc::AsyncClientPtr{async_client_},
                                       absl::optional<std::chrono::milliseconds>(),
                                       "pb.lyft.ratelimit.RateLimitService.ShouldRateLimit"));
    }
  }

  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncRequest async_request_;
  std::unique_ptr<GrpcClientImpl> client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;
};

INSTANTIATE_TEST_CASE_P(RateLimitGrpcClientTest, RateLimitGrpcClientTest,
                        ::testing::Values(true, false));

TEST_P(RateLimitGrpcClientTest, Basic) {
  std::unique_ptr<envoy::service::ratelimit::v2::RateLimitResponse> response;

  {
    const bool use_data_plane_proto = GetParam();
    setClient(use_data_plane_proto);
    envoy::service::ratelimit::v2::RateLimitRequest request;
    Http::HeaderMapImpl headers;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar"}}}});
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), Ref(*client_), _, _))
        .WillOnce(
            Invoke([this, use_data_plane_proto](
                       const Protobuf::MethodDescriptor& service_method, const Protobuf::Message&,
                       Grpc::AsyncRequestCallbacks&, Tracing::Span&,
                       const absl::optional<std::chrono::milliseconds>&) -> Grpc::AsyncRequest* {
              std::string service_name = "envoy.service.ratelimit.v2.RateLimitService";
              if (!use_data_plane_proto) {
                service_name = "pb.lyft.ratelimit.RateLimitService";
              }
              EXPECT_EQ(service_name, service_method.service()->full_name());
              EXPECT_EQ("ShouldRateLimit", service_method.name());
              return &async_request_;
            }));

    client_->limit(request_callbacks_, "foo", {{{{"foo", "bar"}}}}, Tracing::NullSpan::instance());

    client_->onCreateInitialMetadata(headers);
    EXPECT_EQ(nullptr, headers.RequestId());

    response.reset(new envoy::service::ratelimit::v2::RateLimitResponse());
    response->set_overall_code(envoy::service::ratelimit::v2::RateLimitResponse_Code_OVER_LIMIT);
    EXPECT_CALL(span_, setTag("ratelimit_status", "over_limit"));
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OverLimit, _));
    client_->onSuccess(std::move(response), span_);
  }

  {
    envoy::service::ratelimit::v2::RateLimitRequest request;
    Http::HeaderMapImpl headers;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar"}, {"bar", "baz"}}}});
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_->limit(request_callbacks_, "foo", {{{{"foo", "bar"}, {"bar", "baz"}}}},
                   Tracing::NullSpan::instance());

    client_->onCreateInitialMetadata(headers);

    response.reset(new envoy::service::ratelimit::v2::RateLimitResponse());
    response->set_overall_code(envoy::service::ratelimit::v2::RateLimitResponse_Code_OK);
    EXPECT_CALL(span_, setTag("ratelimit_status", "ok"));
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OK, _));
    client_->onSuccess(std::move(response), span_);
  }

  {
    envoy::service::ratelimit::v2::RateLimitRequest request;
    GrpcClientImpl::createRequest(
        request, "foo",
        {{{{"foo", "bar"}, {"bar", "baz"}}}, {{{"foo2", "bar2"}, {"bar2", "baz2"}}}});
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_->limit(request_callbacks_, "foo",
                   {{{{"foo", "bar"}, {"bar", "baz"}}}, {{{"foo2", "bar2"}, {"bar2", "baz2"}}}},
                   Tracing::NullSpan::instance());

    response.reset(new envoy::service::ratelimit::v2::RateLimitResponse());
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::Error, _));
    client_->onFailure(Grpc::Status::Unknown, "", span_);
  }
}

TEST_P(RateLimitGrpcClientTest, Cancel) {
  setClient(GetParam());
  std::unique_ptr<envoy::service::ratelimit::v2::RateLimitResponse> response;

  EXPECT_CALL(*async_client_, send(_, _, _, _, _)).WillOnce(Return(&async_request_));

  client_->limit(request_callbacks_, "foo", {{{{"foo", "bar"}}}}, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

TEST(RateLimitGrpcFactoryTest, Create) {
  envoy::config::ratelimit::v2::RateLimitServiceConfig config;
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("foo");
  Grpc::MockAsyncClientManager async_client_manager;
  Stats::MockStore scope;
  EXPECT_CALL(async_client_manager,
              factoryForGrpcService(ProtoEq(config.grpc_service()), Ref(scope), _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  GrpcFactoryImpl factory(config, async_client_manager, scope);
  factory.create(absl::optional<std::chrono::milliseconds>());
}

// TODO(htuch): cluster_name is deprecated, remove after 1.6.0.
TEST(RateLimitGrpcFactoryTest, CreateLegacy) {
  envoy::config::ratelimit::v2::RateLimitServiceConfig config;
  config.set_cluster_name("foo");
  Grpc::MockAsyncClientManager async_client_manager;
  Stats::MockStore scope;
  envoy::api::v2::core::GrpcService expected_grpc_service;
  expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
  EXPECT_CALL(async_client_manager,
              factoryForGrpcService(ProtoEq(expected_grpc_service), Ref(scope), _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  GrpcFactoryImpl factory(config, async_client_manager, scope);
  factory.create(absl::optional<std::chrono::milliseconds>());
}

TEST(RateLimitNullFactoryTest, Basic) {
  NullFactoryImpl factory;
  ClientPtr client = factory.create(absl::optional<std::chrono::milliseconds>());
  MockRequestCallbacks request_callbacks;
  EXPECT_CALL(request_callbacks, complete_(LimitStatus::OK, _));
  client->limit(request_callbacks, "foo", {{{{"foo", "bar"}}}}, Tracing::NullSpan::instance());
  client->cancel();
}

} // namespace RateLimit
} // namespace Envoy
