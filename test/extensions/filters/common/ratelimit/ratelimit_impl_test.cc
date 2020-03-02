#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/stats/scope.h"

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {
namespace {

class MockRequestCallbacks : public RequestCallbacks {
public:
  void complete(LimitStatus status, Http::ResponseHeaderMapPtr&& response_headers_to_add,
                Http::RequestHeaderMapPtr&& request_headers_to_add) override {
    complete_(status, response_headers_to_add.get(), request_headers_to_add.get());
  }

  MOCK_METHOD(void, complete_,
              (LimitStatus status, const Http::ResponseHeaderMap* response_headers_to_add,
               const Http::RequestHeaderMap* request_headers_to_add));
};

class RateLimitGrpcClientTest : public testing::Test {
public:
  RateLimitGrpcClientTest()
      : async_client_(new Grpc::MockAsyncClient()),
        client_(Grpc::RawAsyncClientPtr{async_client_},
                absl::optional<std::chrono::milliseconds>()) {}

  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImpl client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;
};

TEST_F(RateLimitGrpcClientTest, Basic) {
  std::unique_ptr<envoy::service::ratelimit::v3::RateLimitResponse> response;

  {
    envoy::service::ratelimit::v3::RateLimitRequest request;
    Http::RequestHeaderMapImpl headers;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar"}}}});
    EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(client_), _, _))
        .WillOnce(
            Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                          Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
              std::string service_name = "envoy.service.ratelimit.v2.RateLimitService";
              EXPECT_EQ(service_name, service_full_name);
              EXPECT_EQ("ShouldRateLimit", method_name);
              return &async_request_;
            }));

    client_.limit(request_callbacks_, "foo", {{{{"foo", "bar"}}}}, Tracing::NullSpan::instance());

    client_.onCreateInitialMetadata(headers);
    EXPECT_EQ(nullptr, headers.RequestId());

    response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
    response->set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT);
    EXPECT_CALL(span_, setTag(Eq("ratelimit_status"), Eq("over_limit")));
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OverLimit, _, _));
    client_.onSuccess(std::move(response), span_);
  }

  {
    envoy::service::ratelimit::v3::RateLimitRequest request;
    Http::RequestHeaderMapImpl headers;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar"}, {"bar", "baz"}}}});
    EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_.limit(request_callbacks_, "foo", {{{{"foo", "bar"}, {"bar", "baz"}}}},
                  Tracing::NullSpan::instance());

    client_.onCreateInitialMetadata(headers);

    response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
    response->set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OK);
    EXPECT_CALL(span_, setTag(Eq("ratelimit_status"), Eq("ok")));
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OK, _, _));
    client_.onSuccess(std::move(response), span_);
  }

  {
    envoy::service::ratelimit::v3::RateLimitRequest request;
    GrpcClientImpl::createRequest(
        request, "foo",
        {{{{"foo", "bar"}, {"bar", "baz"}}}, {{{"foo2", "bar2"}, {"bar2", "baz2"}}}});
    EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_.limit(request_callbacks_, "foo",
                  {{{{"foo", "bar"}, {"bar", "baz"}}}, {{{"foo2", "bar2"}, {"bar2", "baz2"}}}},
                  Tracing::NullSpan::instance());

    response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::Error, _, _));
    client_.onFailure(Grpc::Status::Unknown, "", span_);
  }
}

TEST_F(RateLimitGrpcClientTest, Cancel) {
  std::unique_ptr<envoy::service::ratelimit::v3::RateLimitResponse> response;

  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _)).WillOnce(Return(&async_request_));

  client_.limit(request_callbacks_, "foo", {{{{"foo", "bar"}}}}, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

} // namespace
} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
