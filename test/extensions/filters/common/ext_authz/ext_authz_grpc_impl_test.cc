#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::Values;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

using Params = std::tuple<envoy::config::core::v3::ApiVersion>;

class ExtAuthzGrpcClientTest : public testing::TestWithParam<Params> {
public:
  ExtAuthzGrpcClientTest() : async_client_(new Grpc::MockAsyncClient()), timeout_(10) {}

  void initialize(const Params& param) {
    api_version_ = std::get<0>(param);
    client_ = std::make_unique<GrpcClientImpl>(Grpc::RawAsyncClientPtr{async_client_}, timeout_,
                                               api_version_);
  }

  void expectCallSend(envoy::service::auth::v3::CheckRequest& request) {
    EXPECT_CALL(*async_client_,
                sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
        .WillOnce(
            Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                          Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(TestUtility::getVersionedServiceFullName(
                            "envoy.service.auth.{}.Authorization", api_version_),
                        service_full_name);
              EXPECT_EQ("Check", method_name);
              if (Runtime::runtimeFeatureEnabled(
                      "envoy.reloadable_features.ext_authz_measure_timeout_on_check_created")) {
                EXPECT_FALSE(options.timeout.has_value());
              } else {
                EXPECT_EQ(timeout_->count(), options.timeout->count());
              }
              return &async_request_;
            }));
  }

  Grpc::MockAsyncClient* async_client_;
  absl::optional<std::chrono::milliseconds> timeout_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImplPtr client_;
  MockRequestCallbacks request_callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Tracing::MockSpan span_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::ApiVersion api_version_;
};

INSTANTIATE_TEST_SUITE_P(Parameterized, ExtAuthzGrpcClientTest,
                         Values(Params(envoy::config::core::v3::ApiVersion::AUTO),
                                Params(envoy::config::core::v3::ApiVersion::V2),
                                Params(envoy::config::core::v3::ApiVersion::V3)));

// Test the client when an ok response is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationOk) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();

  ProtobufWkt::Struct expected_dynamic_metadata;
  auto* metadata_fields = expected_dynamic_metadata.mutable_fields();
  (*metadata_fields)["foo"] = ValueUtil::stringValue("ok");
  (*metadata_fields)["bar"] = ValueUtil::numberValue(1);

  // The expected dynamic metadata is set to the outer check response, hence regardless the
  // check_response's http_response value (either OkHttpResponse or DeniedHttpResponse) the dynamic
  // metadata is set to be equal to the check response's dynamic metadata.
  check_response->mutable_dynamic_metadata()->MergeFrom(expected_dynamic_metadata);

  status->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);

  // This is the expected authz response.
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;

  authz_response.dynamic_metadata = expected_dynamic_metadata;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an ok response is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationOkWithAllAtributes) {
  initialize(GetParam());

  const std::string empty_body{};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{"foo", "bar", false}});
  auto check_response = TestCommon::makeCheckResponse(
      Grpc::Status::WellKnownGrpcStatus::Ok, envoy::type::v3::OK, empty_body, expected_headers);
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationDenied) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a gRPC status code unknown is received from the authorization server.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationDeniedGrpcUnknownStatus) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::Unknown);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response with additional HTTP attributes is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationDeniedWithAllAttributes) {
  initialize(GetParam());

  const std::string expected_body{"test"};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"foo", "bar", false}, {"foobar", "bar", true}});
  auto check_response =
      TestCommon::makeCheckResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied,
                                    envoy::type::v3::Unauthorized, expected_body, expected_headers);
  auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::Denied, Http::Code::Unauthorized,
                                                      expected_body, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an unknown error occurs.
TEST_P(ExtAuthzGrpcClientTest, UnknownError) {
  initialize(GetParam());

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(Grpc::Status::Unknown, "", span_);
}

// Test the client when the request is canceled.
TEST_P(ExtAuthzGrpcClientTest, CancelledAuthorizationRequest) {
  initialize(GetParam());

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

// Test the client when the request times out.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationRequestTimeout) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.ext_authz_measure_timeout_on_check_created", "false"}});
  initialize(GetParam());

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(Grpc::Status::DeadlineExceeded, "", span_);
}

// Test the client when the request times out on an internal timeout.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationInternalRequestTimeout) {
  initialize(GetParam());
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.ext_authz_measure_timeout_on_check_created", "true"}});

  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer, enableTimer(timeout_.value(), _));

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);

  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  EXPECT_CALL(async_request_, cancel());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzTimedoutResponse())));
  timer->invokeCallback();
}

// Test when the client is cancelled with internal timeout.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationInternalRequestTimeoutCancelled) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.ext_authz_measure_timeout_on_check_created", "true"}});

  initialize(GetParam());

  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer, enableTimer(timeout_.value(), _));

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);

  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  EXPECT_CALL(async_request_, cancel());
  EXPECT_CALL(request_callbacks_, onComplete_(_)).Times(0);
  // make sure cancel resets the timer:
  bool timer_destroyed = false;
  timer->timer_destroyed_ = &timer_destroyed;
  client_->cancel();
  EXPECT_EQ(timer_destroyed, true);
}

// Test the client when an OK response is received with dynamic metadata in that OK response.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationOkWithDynamicMetadata) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();

  ProtobufWkt::Struct expected_dynamic_metadata;
  auto* metadata_fields = expected_dynamic_metadata.mutable_fields();
  (*metadata_fields)["original"] = ValueUtil::stringValue("true");
  check_response->mutable_dynamic_metadata()->MergeFrom(expected_dynamic_metadata);

  ProtobufWkt::Struct overridden_dynamic_metadata;
  metadata_fields = overridden_dynamic_metadata.mutable_fields();
  (*metadata_fields)["original"] = ValueUtil::stringValue("false");

  check_response->mutable_ok_response()->mutable_dynamic_metadata()->MergeFrom(
      overridden_dynamic_metadata);

  status->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);

  // This is the expected authz response.
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;
  authz_response.dynamic_metadata = overridden_dynamic_metadata;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, dispatcher_, request, Tracing::NullSpan::instance(),
                 stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

class AsyncClientCacheTest : public testing::Test {
public:
  AsyncClientCacheTest() {
    client_cache_ = std::make_unique<AsyncClientCache>(async_client_manager_, scope_, tls_);
  }

  void expectClientCreation() {
    factory_ = new Grpc::MockAsyncClientFactory;
    async_client_ = new Grpc::MockAsyncClient;
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, true))
        .WillOnce(Invoke([this](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
            return Grpc::RawAsyncClientPtr{async_client_};
          }));
          return Grpc::AsyncClientFactoryPtr{factory_};
        }));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Grpc::MockAsyncClientManager async_client_manager_;
  Grpc::MockAsyncClient* async_client_ = nullptr;
  Grpc::MockAsyncClientFactory* factory_ = nullptr;
  std::unique_ptr<AsyncClientCache> client_cache_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

TEST_F(AsyncClientCacheTest, Deduplication) {
  Stats::IsolatedStoreImpl scope;
  testing::InSequence s;

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz config;
  config.mutable_grpc_service()->mutable_google_grpc()->set_target_uri("dns://test01");
  config.mutable_grpc_service()->mutable_google_grpc()->set_credentials_factory_name(
      "test_credential01");

  expectClientCreation();
  Grpc::RawAsyncClientSharedPtr test_client_01 = client_cache_->getOrCreateAsyncClient(config);
  // Fetches the existing client.
  EXPECT_EQ(test_client_01, client_cache_->getOrCreateAsyncClient(config));

  config.mutable_grpc_service()->mutable_google_grpc()->set_credentials_factory_name(
      "test_credential02");
  expectClientCreation();
  // Different credentials use different clients.
  EXPECT_NE(test_client_01, client_cache_->getOrCreateAsyncClient(config));
  Grpc::RawAsyncClientSharedPtr test_client_02 = client_cache_->getOrCreateAsyncClient(config);

  config.mutable_grpc_service()->mutable_google_grpc()->set_credentials_factory_name(
      "test_credential02");
  // No creation, fetching the existing one.
  EXPECT_EQ(test_client_02, client_cache_->getOrCreateAsyncClient(config));

  // Different targets use different clients.
  config.mutable_grpc_service()->mutable_google_grpc()->set_target_uri("dns://test02");
  expectClientCreation();
  EXPECT_NE(test_client_01, client_cache_->getOrCreateAsyncClient(config));
  EXPECT_NE(test_client_02, client_cache_->getOrCreateAsyncClient(config));
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
