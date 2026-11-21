#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/tracing/tracer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"
#include "source/common/grpc/common.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/http/ai_filters/model_router/async_rls.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/mocks/grpc/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

using envoy::service::ratelimit::v3::RateLimitRequest;
using envoy::service::ratelimit::v3::RateLimitResponse;
using StatusHelpers::HasStatus;
using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOk;
using testing::_;
using testing::NiceMock;
using testing::StrictMock;

class AsyncRlsTest : public testing::Test {
protected:
  AsyncRlsTest()
      : executor_(std::make_shared<Coroutine::ManualExecutor>()),
        mock_client_(std::make_shared<StrictMock<Grpc::MockAsyncClient>>()) {}

  static RateLimitRequest makeRequest(const std::string& domain = "model_router") {
    RateLimitRequest req;
    req.set_domain(domain);
    auto* desc = req.add_descriptors();
    auto* entry = desc->add_entries();
    entry->set_key("model");
    entry->set_value("gpt-4o");
    desc->mutable_hits_addend()->set_value(1);
    return req;
  }

  std::shared_ptr<Coroutine::ManualExecutor> executor_;
  std::shared_ptr<StrictMock<Grpc::MockAsyncClient>> mock_client_;
};

TEST_F(AsyncRlsTest, CheckRateLimitSuccess) {
  AsyncRlsClient client(mock_client_, std::chrono::milliseconds(500));
  const RateLimitRequest expected_req = makeRequest();

  Grpc::RawAsyncRequestCallbacks* captured_callbacks = nullptr;
  StrictMock<Grpc::MockAsyncRequest> async_request;

  EXPECT_CALL(*mock_client_,
              sendRaw("envoy.service.ratelimit.v3.RateLimitService", "CheckRateLimit", _, _, _, _))
      .WillOnce([&](absl::string_view, absl::string_view, Buffer::InstancePtr&& request,
                    Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span&,
                    const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
        EXPECT_EQ(options.timeout, std::chrono::milliseconds(500));
        RateLimitRequest actual_req;
        EXPECT_TRUE(actual_req.ParseFromString(request->toString()));
        EXPECT_TRUE(TestUtility::protoEqual(actual_req, expected_req));
        captured_callbacks = &callbacks;
        return &async_request;
      });

  std::optional<absl::StatusOr<RateLimitResponse>> result;
  auto handle = Coroutine::launch(
      client.checkRateLimit(expected_req), executor_,
      [&result](absl::StatusOr<RateLimitResponse> val) { result.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);

  ASSERT_NE(captured_callbacks, nullptr);
  EXPECT_FALSE(result.has_value());

  Http::TestRequestHeaderMapImpl metadata;
  captured_callbacks->onCreateInitialMetadata(metadata);

  RateLimitResponse expected_resp;
  expected_resp.set_overall_code(RateLimitResponse::OK);
  captured_callbacks->onSuccessRaw(Grpc::Common::serializeMessage(expected_resp),
                                   Tracing::NullSpan::instance());
  executor_->drain();

  ASSERT_TRUE(result.has_value());
  ASSERT_THAT(*result, IsOk());
  EXPECT_EQ((*result)->overall_code(), RateLimitResponse::OK);
}

TEST_F(AsyncRlsTest, ConsumeRateLimitSuccess) {
  AsyncRlsClient client(mock_client_);
  const RateLimitRequest expected_req = makeRequest("custom_domain");

  Grpc::RawAsyncRequestCallbacks* captured_callbacks = nullptr;
  StrictMock<Grpc::MockAsyncRequest> async_request;

  EXPECT_CALL(*mock_client_, sendRaw("envoy.service.ratelimit.v3.RateLimitService",
                                     "ConsumeRateLimit", _, _, _, _))
      .WillOnce([&](absl::string_view, absl::string_view, Buffer::InstancePtr&& request,
                    Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span&,
                    const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
        EXPECT_EQ(options.timeout, AsyncRlsClient::DefaultTimeout);
        RateLimitRequest actual_req;
        EXPECT_TRUE(actual_req.ParseFromString(request->toString()));
        EXPECT_TRUE(TestUtility::protoEqual(actual_req, expected_req));
        captured_callbacks = &callbacks;
        return &async_request;
      });

  std::optional<absl::StatusOr<RateLimitResponse>> result;
  auto handle = Coroutine::launch(
      client.consumeRateLimit(expected_req), executor_,
      [&result](absl::StatusOr<RateLimitResponse> val) { result.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);

  ASSERT_NE(captured_callbacks, nullptr);
  RateLimitResponse expected_resp;
  expected_resp.set_overall_code(RateLimitResponse::OK);
  captured_callbacks->onSuccessRaw(Grpc::Common::serializeMessage(expected_resp),
                                   Tracing::NullSpan::instance());
  executor_->drain();

  ASSERT_TRUE(result.has_value());
  ASSERT_THAT(*result, IsOk());
}

TEST_F(AsyncRlsTest, SynchronousFailureInOnStart) {
  AsyncRlsClient client(mock_client_);

  EXPECT_CALL(*mock_client_,
              sendRaw("envoy.service.ratelimit.v3.RateLimitService", "CheckRateLimit", _, _, _, _))
      .WillOnce([](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                   Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& span,
                   const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        callbacks.onFailure(Grpc::Status::WellKnownGrpcStatus::Unavailable, "Cluster not available",
                            span);
        return nullptr;
      });

  std::optional<absl::StatusOr<RateLimitResponse>> result;
  auto handle = Coroutine::launch(
      client.checkRateLimit(makeRequest()), executor_,
      [&result](absl::StatusOr<RateLimitResponse> val) { result.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);
  executor_->drain();

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, HasStatus(absl::StatusCode::kUnavailable, "Cluster not available"));
}

TEST_F(AsyncRlsTest, AsynchronousFailureAndInvalidGrpcStatusCode) {
  AsyncRlsClient client(mock_client_);

  Grpc::RawAsyncRequestCallbacks* captured_callbacks = nullptr;
  StrictMock<Grpc::MockAsyncRequest> async_request;

  EXPECT_CALL(*mock_client_,
              sendRaw("envoy.service.ratelimit.v3.RateLimitService", "CheckRateLimit", _, _, _, _))
      .WillRepeatedly([&](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                          Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        captured_callbacks = &callbacks;
        return &async_request;
      });

  // 1. Empty message falls back to Grpc::Utility::grpcStatusToString.
  std::optional<absl::StatusOr<RateLimitResponse>> result1;
  auto handle1 = Coroutine::launch(
      client.checkRateLimit(makeRequest()), executor_,
      [&result1](absl::StatusOr<RateLimitResponse> val) { result1.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);
  ASSERT_NE(captured_callbacks, nullptr);
  captured_callbacks->onFailure(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded, "",
                                Tracing::NullSpan::instance());
  executor_->drain();
  ASSERT_TRUE(result1.has_value());
  EXPECT_THAT(*result1, HasStatus(absl::StatusCode::kDeadlineExceeded, "DeadlineExceeded"));

  // 2. Out-of-range GrpcStatus maps to kUnknown.
  std::optional<absl::StatusOr<RateLimitResponse>> result2;
  auto handle2 = Coroutine::launch(
      client.checkRateLimit(makeRequest()), executor_,
      [&result2](absl::StatusOr<RateLimitResponse> val) { result2.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);
  ASSERT_NE(captured_callbacks, nullptr);
  captured_callbacks->onFailure(Grpc::Status::WellKnownGrpcStatus::InvalidCode, "bad status",
                                Tracing::NullSpan::instance());
  executor_->drain();
  ASSERT_TRUE(result2.has_value());
  EXPECT_THAT(*result2, HasStatus(absl::StatusCode::kUnknown, "bad status"));
}

TEST_F(AsyncRlsTest, NullClientFailsImmediately) {
  AsyncRlsClient client(nullptr);

  std::optional<absl::StatusOr<RateLimitResponse>> result;
  auto handle = Coroutine::launch(
      client.checkRateLimit(makeRequest()), executor_,
      [&result](absl::StatusOr<RateLimitResponse> val) { result.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);
  executor_->drain();

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, HasStatus(absl::StatusCode::kUnavailable, "gRPC client is not initialized"));
}

TEST_F(AsyncRlsTest, CancelInflightRequest) {
  AsyncRlsClient client(mock_client_);
  StrictMock<Grpc::MockAsyncRequest> async_request;

  EXPECT_CALL(*mock_client_,
              sendRaw("envoy.service.ratelimit.v3.RateLimitService", "CheckRateLimit", _, _, _, _))
      .WillOnce([&](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                    Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                    const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        return &async_request;
      });
  EXPECT_CALL(async_request, cancel());

  std::optional<absl::StatusOr<RateLimitResponse>> result;
  auto handle = Coroutine::launch(
      client.checkRateLimit(makeRequest()), executor_,
      [&result](absl::StatusOr<RateLimitResponse> val) { result.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);

  EXPECT_FALSE(result.has_value());
  handle.cancel();
  executor_->drain();

  ASSERT_TRUE(result.has_value());
  EXPECT_THAT(*result, HasStatusCode(absl::StatusCode::kCancelled));
}

TEST_F(AsyncRlsTest, ConcurrentInflightRequestsCompleteOutOfOrder) {
  AsyncRlsClient client(mock_client_);

  Grpc::RawAsyncRequestCallbacks* callbacks_1 = nullptr;
  Grpc::RawAsyncRequestCallbacks* callbacks_2 = nullptr;
  StrictMock<Grpc::MockAsyncRequest> req_1;
  StrictMock<Grpc::MockAsyncRequest> req_2;

  EXPECT_CALL(*mock_client_,
              sendRaw("envoy.service.ratelimit.v3.RateLimitService", "CheckRateLimit", _, _, _, _))
      .WillOnce([&](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                    Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span&,
                    const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        callbacks_1 = &callbacks;
        return &req_1;
      });
  EXPECT_CALL(*mock_client_, sendRaw("envoy.service.ratelimit.v3.RateLimitService",
                                     "ConsumeRateLimit", _, _, _, _))
      .WillOnce([&](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                    Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span&,
                    const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        callbacks_2 = &callbacks;
        return &req_2;
      });

  std::optional<absl::StatusOr<RateLimitResponse>> res_1;
  std::optional<absl::StatusOr<RateLimitResponse>> res_2;

  auto handle_1 = Coroutine::launch(
      client.checkRateLimit(makeRequest()), executor_,
      [&res_1](absl::StatusOr<RateLimitResponse> val) { res_1.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);
  auto handle_2 = Coroutine::launch(
      client.consumeRateLimit(makeRequest()), executor_,
      [&res_2](absl::StatusOr<RateLimitResponse> val) { res_2.emplace(std::move(val)); },
      Coroutine::StartMode::Inline);

  ASSERT_NE(callbacks_1, nullptr);
  ASSERT_NE(callbacks_2, nullptr);

  RateLimitResponse ok_resp;
  ok_resp.set_overall_code(RateLimitResponse::OK);
  callbacks_2->onSuccessRaw(Grpc::Common::serializeMessage(ok_resp), Tracing::NullSpan::instance());
  executor_->drain();
  EXPECT_FALSE(res_1.has_value());
  ASSERT_TRUE(res_2.has_value());
  EXPECT_THAT(*res_2, IsOk());

  RateLimitResponse over_limit_resp;
  over_limit_resp.set_overall_code(RateLimitResponse::OVER_LIMIT);
  callbacks_1->onSuccessRaw(Grpc::Common::serializeMessage(over_limit_resp),
                            Tracing::NullSpan::instance());
  executor_->drain();
  ASSERT_TRUE(res_1.has_value());
  ASSERT_THAT(*res_1, IsOk());
  EXPECT_EQ((*res_1)->overall_code(), RateLimitResponse::OVER_LIMIT);
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
