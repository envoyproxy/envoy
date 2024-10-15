#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

using Params = std::tuple<envoy::config::core::v3::ApiVersion>;

class ExtAuthzGrpcClientTest : public testing::Test {
public:
  ExtAuthzGrpcClientTest() : async_client_(new Grpc::MockAsyncClient()), timeout_(10) {}

  void initialize() {
    client_ = std::make_unique<GrpcClientImpl>(Grpc::RawAsyncClientPtr{async_client_}, timeout_);
  }

  void expectCallSend(envoy::service::auth::v3::CheckRequest& request) {
    EXPECT_CALL(*async_client_,
                sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
        .WillOnce(
            Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                          Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ("envoy.service.auth.v3.Authorization", service_full_name);
              EXPECT_EQ("Check", method_name);
              EXPECT_EQ(timeout_->count(), options.timeout->count());
              return &async_request_;
            }));
  }

  Grpc::MockAsyncClient* async_client_;
  absl::optional<std::chrono::milliseconds> timeout_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImplPtr client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::ApiVersion api_version_;
};

// Test the client when an ok response is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOk) {
  initialize();

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
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

TEST_F(ExtAuthzGrpcClientTest, StreamInfo) {
  initialize();

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  NiceMock<StreamInfo::MockStreamInfo> ext_authz_stream_info;
  EXPECT_CALL(async_request_, streamInfo()).WillOnce(ReturnRef(ext_authz_stream_info));
  EXPECT_NE(client_->streamInfo(), nullptr);

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

// Test the client when an ok response is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithAllAtributes) {
  initialize();

  const std::string empty_body{};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{"foo", "bar", false}});
  const auto expected_downstream_headers = TestCommon::makeHeaderValueOption(
      {{"authorized-by", "TestAuthService", false}, {"cookie", "authtoken=1234", true}});
  auto check_response =
      TestCommon::makeCheckResponse(Grpc::Status::WellKnownGrpcStatus::Ok, envoy::type::v3::OK,
                                    empty_body, expected_headers, expected_downstream_headers);
  auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::OK, Http::Code::OK, empty_body, expected_headers, expected_downstream_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

// Test that the client just passes through invalid headers (they will fail validation in the filter
// later).
TEST_F(ExtAuthzGrpcClientTest, IndifferentToInvalidHeaders) {
  initialize();

  const std::string empty_body{};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{"foo", "bar", false}});
  const auto expected_downstream_headers = TestCommon::makeHeaderValueOption(
      {{"invalid-key\n\n\n\n\n", "TestAuthService", false}, {"cookie", "authtoken=1234", true}});
  auto check_response =
      TestCommon::makeCheckResponse(Grpc::Status::WellKnownGrpcStatus::Ok, envoy::type::v3::OK,
                                    empty_body, expected_headers, expected_downstream_headers);
  auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::OK, Http::Code::OK, empty_body, expected_headers, expected_downstream_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationDenied) {
  initialize();

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a gRPC status code unknown is received from the authorization server.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationDeniedGrpcUnknownStatus) {
  initialize();

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::Unknown);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response with additional HTTP attributes is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationDeniedWithAllAttributes) {
  initialize();

  const std::string expected_body{"test"};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"foo", "bar", false}, {"foobar", "bar", true}});
  const auto expected_downstream_headers = TestCommon::makeHeaderValueOption({});
  auto check_response = TestCommon::makeCheckResponse(
      Grpc::Status::WellKnownGrpcStatus::PermissionDenied, envoy::type::v3::Unauthorized,
      expected_body, expected_headers, expected_downstream_headers);
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::Denied, Http::Code::Unauthorized, expected_body,
                                    expected_headers, expected_downstream_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response with unknown HTTP status code (i.e. if
// DeniedResponse.status is not set by the auth server implementation). The response sent to client
// is set with the default HTTP status code for denied response (403 Forbidden).
TEST_F(ExtAuthzGrpcClientTest, AuthorizationDeniedWithEmptyDeniedResponseStatus) {
  initialize();

  const std::string expected_body{"test"};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"foo", "bar", false}, {"foobar", "bar", true}});
  const auto expected_downstream_headers = TestCommon::makeHeaderValueOption({});
  auto check_response = TestCommon::makeCheckResponse(
      Grpc::Status::WellKnownGrpcStatus::PermissionDenied, envoy::type::v3::Empty, expected_body,
      expected_headers, expected_downstream_headers);
  // When the check response gives unknown denied response HTTP status code, the filter sets the
  // response HTTP status code with 403 Forbidden (default).
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::Denied, Http::Code::Forbidden, expected_body,
                                    expected_headers, expected_downstream_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an unknown error occurs.
TEST_F(ExtAuthzGrpcClientTest, UnknownError) {
  initialize();

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(Grpc::Status::Unknown, "", span_);
}

// Test the client when the request is canceled.
TEST_F(ExtAuthzGrpcClientTest, CancelledAuthorizationRequest) {
  initialize();

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

// Test the client when the request times out.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationRequestTimeout) {
  initialize();

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(Grpc::Status::DeadlineExceeded, "", span_);
}

// Test the client when an OK response is received with dynamic metadata in that OK response.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithDynamicMetadata) {
  initialize();

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
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an OK response is received with additional query string parameters.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithQueryParameters) {
  initialize();

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();

  status->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);

  const Http::Utility::QueryParamsVector query_parameters_to_set{{"add-me", "yes"}};
  for (const auto& [key, value] : query_parameters_to_set) {
    auto* query_parameter = check_response->mutable_ok_response()->add_query_parameters_to_set();
    query_parameter->set_key(key);
    query_parameter->set_value(value);
  }

  const std::vector<std::string> query_parameters_to_remove{"remove-me"};
  for (const auto& key : query_parameters_to_remove) {
    check_response->mutable_ok_response()->add_query_parameters_to_remove(key);
  }

  // This is the expected authz response.
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;
  authz_response.query_parameters_to_set = {{"add-me", "yes"}};
  authz_response.query_parameters_to_remove = {"remove-me"};

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithAppendActions) {
  initialize();

  envoy::service::auth::v3::CheckResponse check_response;
  TestUtility::loadFromYaml(R"EOF(
status:
  code: 0
ok_response:
  response_headers_to_add:
  - header:
      key: append-if-exists-or-add
      value: append-if-exists-or-add-value
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: add-if-absent
      value: add-if-absent-value
    append_action: ADD_IF_ABSENT
  - header:
      key: overwrite-if-exists
      value: overwrite-if-exists-value
    append_action: OVERWRITE_IF_EXISTS
  - header:
      key: overwrite-if-exists-or-add
      value: overwrite-if-exists-or-add-value
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
)EOF",
                            check_response);

  auto expected_authz_response = Response{
      .status = CheckStatus::OK,
      .response_headers_to_add =
          UnsafeHeaderVector{{"append-if-exists-or-add", "append-if-exists-or-add-value"}},
      .response_headers_to_set =
          UnsafeHeaderVector{{"overwrite-if-exists-or-add", "overwrite-if-exists-or-add-value"}},
      .response_headers_to_add_if_absent =
          UnsafeHeaderVector{{"add-if-absent", "add-if-absent-value"}},
      .response_headers_to_overwrite_if_exists =
          UnsafeHeaderVector{{"overwrite-if-exists", "overwrite-if-exists-value"}},
      .status_code = Http::Code::OK,
  };

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzOkResponse(expected_authz_response))));
  client_->onSuccess(std::make_unique<envoy::service::auth::v3::CheckResponse>(check_response),
                     span_);
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
