#include <memory>

#include "envoy/service/redis_auth/v3/redis_external_auth.pb.h"

#include "source/extensions/filters/network/redis_proxy/external_auth.h"

#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"

#include "gtest/gtest.h"

using testing::Eq;
using testing::Ref;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ExternalAuth {

class GrpcExternalAuthClientTest : public testing::Test {
public:
  GrpcExternalAuthClientTest() : async_client_(new Grpc::MockAsyncClient()), timeout_(10) {}

  void initialize() {
    client_ =
        std::make_unique<GrpcExternalAuthClient>(Grpc::RawAsyncClientPtr{async_client_}, timeout_);
  }

  void expectCallSend(envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest& request) {
    EXPECT_CALL(*async_client_,
                sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
        .WillOnce(
            Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                          Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ("envoy.service.redis_auth.v3.RedisProxyExternalAuth", service_full_name);
              EXPECT_EQ("Authenticate", method_name);
              EXPECT_EQ(timeout_->count(), options.timeout->count());
              return &async_request_;
            }));
  }

  Grpc::MockAsyncClient* async_client_;
  absl::optional<std::chrono::milliseconds> timeout_;
  GrpcExternalAuthClientPtr client_;
  Grpc::MockAsyncRequest async_request_;
  Tracing::MockSpan span_;
  MockAuthenticateCallback request_callback_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  CommandSplitter::MockSplitCallbacks pending_request_;
};

TEST_F(GrpcExternalAuthClientTest, SuccessOk) {
  initialize();

  auto response =
      std::make_unique<envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>();
  response->mutable_expiration()->set_seconds(42);
  auto status = response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);

  auto expected_response = AuthenticateResponse{};
  expected_response.expiration.set_seconds(42);
  expected_response.status = AuthenticationRequestStatus::Authorized;

  envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest request;
  request.set_username("username");
  request.set_password("password");
  expectCallSend(request);
  client_->authenticateExternal(request_callback_, pending_request_, stream_info_, "username",
                                "password");

  EXPECT_CALL(span_, setTag(Eq("redis_auth_status"), Eq("redis_auth_ok")));
  EXPECT_CALL(request_callback_, onAuthenticateExternal_(_, _));
  client_->onSuccess(std::move(response), span_);
}

TEST_F(GrpcExternalAuthClientTest, SuccessPermissionDenied) {
  initialize();

  auto response =
      std::make_unique<envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>();
  response->mutable_expiration()->set_seconds(42);
  auto status = response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  auto expected_response = AuthenticateResponse{};
  expected_response.expiration.set_seconds(42);
  expected_response.status = AuthenticationRequestStatus::Unauthorized;

  envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest request;
  request.set_username("username");
  request.set_password("password");
  expectCallSend(request);
  client_->authenticateExternal(request_callback_, pending_request_, stream_info_, "username",
                                "password");

  EXPECT_CALL(span_, setTag(Eq("redis_auth_status"), Eq("redis_auth_unauthorized")));
  EXPECT_CALL(request_callback_, onAuthenticateExternal_(_, _));
  client_->onSuccess(std::move(response), span_);
}

TEST_F(GrpcExternalAuthClientTest, SuccessUnauthenticated) {
  initialize();

  auto response =
      std::make_unique<envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>();
  response->mutable_expiration()->set_seconds(42);
  auto status = response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::Unauthenticated);

  auto expected_response = AuthenticateResponse{};
  expected_response.expiration.set_seconds(42);
  expected_response.status = AuthenticationRequestStatus::Unauthorized;

  envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest request;
  request.set_username("username");
  request.set_password("password");
  expectCallSend(request);
  client_->authenticateExternal(request_callback_, pending_request_, stream_info_, "username",
                                "password");

  EXPECT_CALL(span_, setTag(Eq("redis_auth_status"), Eq("redis_auth_unauthorized")));
  EXPECT_CALL(request_callback_, onAuthenticateExternal_(_, _));
  client_->onSuccess(std::move(response), span_);
}

TEST_F(GrpcExternalAuthClientTest, SuccessUnknown) {
  initialize();

  auto response =
      std::make_unique<envoy::service::redis_auth::v3::RedisProxyExternalAuthResponse>();
  response->mutable_expiration()->set_seconds(42);
  auto status = response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::Unknown);
  response->set_message("oops, unknown");

  auto expected_response = AuthenticateResponse{};
  expected_response.expiration.set_seconds(42);
  expected_response.status = AuthenticationRequestStatus::Error;
  expected_response.message = "oops, unknown";

  envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest request;
  request.set_username("username");
  request.set_password("password");
  expectCallSend(request);
  client_->authenticateExternal(request_callback_, pending_request_, stream_info_, "username",
                                "password");

  EXPECT_CALL(span_, setTag(Eq("redis_auth_status"), Eq("redis_auth_error")));
  EXPECT_CALL(request_callback_, onAuthenticateExternal_(_, _));
  client_->onSuccess(std::move(response), span_);
}

TEST_F(GrpcExternalAuthClientTest, Failure) {
  initialize();

  auto expected_response = AuthenticateResponse{};
  expected_response.status = AuthenticationRequestStatus::Error;
  expected_response.message = "error message";

  envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest request;
  request.set_username("username");
  request.set_password("password");
  expectCallSend(request);
  client_->authenticateExternal(request_callback_, pending_request_, stream_info_, "username",
                                "password");

  EXPECT_CALL(span_, setTag(Eq("redis_auth_status"), Eq("redis_auth_error")));
  EXPECT_CALL(request_callback_, onAuthenticateExternal_(_, _));
  client_->onFailure(Grpc::Status::Unknown, "error message", span_);

  // no-op
  Http::RequestHeaderMapPtr hm = Http::RequestHeaderMapImpl::create();
  client_->onCreateInitialMetadata(*hm);
}

TEST_F(GrpcExternalAuthClientTest, Cancel) {
  initialize();

  envoy::service::redis_auth::v3::RedisProxyExternalAuthRequest request;
  request.set_username("username");
  request.set_password("password");
  expectCallSend(request);
  client_->authenticateExternal(request_callback_, pending_request_, stream_info_, "username",
                                "password");

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

} // namespace ExternalAuth
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
