#include "envoy/api/v2/core/base.pb.h"

#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::WhenDynamicCastTo;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

class ExtAuthzGrpcClientTest : public testing::Test {
public:
  ExtAuthzGrpcClientTest()
      : async_client_(new Grpc::MockAsyncClient()), timeout_(10),
        client_(Grpc::AsyncClientPtr{async_client_}, timeout_) {}

  Grpc::MockAsyncClient* async_client_;
  absl::optional<std::chrono::milliseconds> timeout_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImpl client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;

  void expectCallSend(envoy::service::auth::v2alpha::CheckRequest& request) {
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), Ref(client_), _, _))
        .WillOnce(Invoke(
            [this](
                const Protobuf::MethodDescriptor& service_method, const Protobuf::Message&,
                Grpc::AsyncRequestCallbacks&, Tracing::Span&,
                const absl::optional<std::chrono::milliseconds>& timeout) -> Grpc::AsyncRequest* {
              // TODO(dio): Use a defined constant value.
              EXPECT_EQ("envoy.service.auth.v2alpha.Authorization",
                        service_method.service()->full_name());
              EXPECT_EQ("Check", service_method.name());
              EXPECT_EQ(timeout_->count(), timeout->count());
              return &async_request_;
            }));
  }
};

// Test the client when an ok response is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOk) {
  auto check_response = std::make_unique<envoy::service::auth::v2alpha::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::GrpcStatus::Ok);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;

  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  Http::HeaderMapImpl headers;
  client_.onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_ok"));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));
  client_.onSuccess(std::move(check_response), span_);
}

// Test the client when an ok response is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithAllAtributes) {
  const std::string empty_body{};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{"foo", "bar", false}});
  auto check_response = TestCommon::makeCheckResponse(
      Grpc::Status::GrpcStatus::Ok, envoy::type::StatusCode::OK, empty_body, expected_headers);
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  Http::HeaderMapImpl headers;
  client_.onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_ok"));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_.onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationDenied) {
  auto check_response = std::make_unique<envoy::service::auth::v2alpha::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::GrpcStatus::PermissionDenied);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;

  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  Http::HeaderMapImpl headers;
  client_.onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_unauthorized"));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));

  client_.onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response with additional HTTP attributes is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationDeniedWithAllAttributes) {
  const std::string expected_body{"test"};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"foo", "bar", false}, {"foobar", "bar", true}});
  auto check_response = TestCommon::makeCheckResponse(Grpc::Status::GrpcStatus::PermissionDenied,
                                                      envoy::type::StatusCode::Unauthorized,
                                                      expected_body, expected_headers);
  auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::Denied, Http::Code::Unauthorized,
                                                      expected_body, expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  Http::HeaderMapImpl headers;
  client_.onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_unauthorized"));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(std::move(check_response), span_);
}

// Test the client when an unknown error occurs.
TEST_F(ExtAuthzGrpcClientTest, UnknownError) {
  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_.onFailure(Grpc::Status::Unknown, "", span_);
}

// Test the client when the request is canceled.
TEST_F(ExtAuthzGrpcClientTest, CancelledAuthorizationRequest) {
  envoy::service::auth::v2alpha::CheckRequest request;
  EXPECT_CALL(*async_client_, send(_, _, _, _, _)).WillOnce(Return(&async_request_));
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

// Test the client when the request times out.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationRequestTimeout) {
  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_.onFailure(Grpc::Status::DeadlineExceeded, "", span_);
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
