#include "envoy/config/core/v3/base.pb.h"
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

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
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

  void initializeWithRawHeaderValue() {
    client_ = std::make_unique<GrpcClientImpl>(Grpc::RawAsyncClientPtr{async_client_}, timeout_,
                                               /*emit_client_span=*/true,
                                               /*read_raw_header_value=*/true);
  }

  void expectCallSend(envoy::service::auth::v3::CheckRequest& request) {
    EXPECT_CALL(*async_client_,
                sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
        .WillOnce([this](absl::string_view service_full_name, absl::string_view method_name,
                         Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                         const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
          EXPECT_EQ("envoy.service.auth.v3.Authorization", service_full_name);
          EXPECT_EQ("Check", method_name);
          EXPECT_EQ(timeout_->count(), options.timeout->count());
          return &async_request_;
        });
  }

  Grpc::MockAsyncClient* async_client_;
  std::optional<std::chrono::milliseconds> timeout_;
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

  auto check_response = CheckResponsePtr();
  auto status = check_response->mutable_status();

  Protobuf::Struct expected_dynamic_metadata;
  auto* metadata_fields = expected_dynamic_metadata.mutable_fields();
  (*metadata_fields)["foo"] = ValueUtil::stringValue("ok");
  (*metadata_fields)["bar"] = ValueUtil::numberValue(1);

  // The expected dynamic metadata is set to the outer check response, hence regardless the
  // check_response's http_response value (either OkHttpResponse or DeniedHttpResponse) the dynamic
  // metadata is set to be equal to the check response's dynamic metadata.
  check_response->mutable_dynamic_metadata()->MergeFrom(expected_dynamic_metadata);
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
  status->set_code(grpc_status);

  // This is the expected authz response.
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;
  authz_response.grpc_status = grpc_status;

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
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
  auto check_response = TestCommon::makeCheckResponse(
      grpc_status, envoy::type::v3::OK, empty_body, expected_headers, expected_downstream_headers);
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers,
                                    expected_downstream_headers, grpc_status);

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
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
  auto check_response = TestCommon::makeCheckResponse(
      grpc_status, envoy::type::v3::OK, empty_body, expected_headers, expected_downstream_headers);
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers,
                                    expected_downstream_headers, grpc_status);

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

  auto check_response = CheckResponsePtr();
  auto status = check_response->mutable_status();
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::PermissionDenied;
  status->set_code(grpc_status);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;
  authz_response.grpc_status = grpc_status;

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

  auto check_response = CheckResponsePtr();
  auto status = check_response->mutable_status();
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Unknown;
  status->set_code(grpc_status);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;
  authz_response.grpc_status = grpc_status;

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
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::PermissionDenied;
  auto check_response =
      TestCommon::makeCheckResponse(grpc_status, envoy::type::v3::Unauthorized, expected_body,
                                    expected_headers, expected_downstream_headers);
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::Denied, Http::Code::Unauthorized, expected_body,
                                    expected_headers, expected_downstream_headers, grpc_status);

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
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::PermissionDenied;
  auto check_response =
      TestCommon::makeCheckResponse(grpc_status, envoy::type::v3::Empty, expected_body,
                                    expected_headers, expected_downstream_headers);
  // When the check response gives unknown denied response HTTP status code, the filter sets the
  // response HTTP status code with 403 Forbidden (default).
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::Denied, Http::Code::Forbidden, expected_body,
                                    expected_headers, expected_downstream_headers, grpc_status);

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

  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Unknown;
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Error;
  authz_response.grpc_status = grpc_status;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(authz_response))));
  client_->onFailure(grpc_status, "", span_);
}

// Test that gRPC call failure (onFailure) leaves status_code unset (0).
// This allows the filter to use status_on_error config instead of a hardcoded value.
TEST_F(ExtAuthzGrpcClientTest, GrpcCallFailureDoesNotSetStatusCode) {
  initialize();

  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Unavailable;
  // Expected: status_code should be unset (0), not Forbidden.
  auto expected_response = Response{};
  expected_response.status = CheckStatus::Error;
  expected_response.status_code = {}; // Unset
  expected_response.grpc_status = grpc_status;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzErrorResponseWithAttributes(expected_response))));
  client_->onFailure(grpc_status, "", span_);
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

  const auto grpc_status = Grpc::Status::DeadlineExceeded;
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Error;
  authz_response.grpc_status = grpc_status;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(authz_response))));
  client_->onFailure(grpc_status, "", span_);
}

// Test the client when an error response with custom attributes is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationErrorWithAllAttributes) {
  initialize();

  const std::string expected_body{"Internal Server Error"};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"x-error-code", "AUTH_SERVICE_ERROR", false}});
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Internal;
  auto check_response = TestCommon::makeErrorCheckResponse(
      grpc_status, envoy::type::v3::InternalServerError, expected_body, expected_headers);
  auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Error, Http::Code::InternalServerError, expected_body, expected_headers,
      HeaderValueOptionVector{}, grpc_status);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_error")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzErrorResponseWithAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an error response with empty status code is received.
// The response sent to client should use the status_on_error configuration.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationErrorWithEmptyErrorResponseStatus) {
  initialize();

  const std::string expected_body{"Service Unavailable"};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"x-error-message", "auth backend down", false}});
  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Unavailable;
  auto check_response = TestCommon::makeErrorCheckResponse(grpc_status, envoy::type::v3::Empty,
                                                           expected_body, expected_headers);
  // When the error_response has no HTTP status code, the gRPC client doesn't set a default.
  // The filter will use status_on_error configuration instead.
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::Error, static_cast<Http::Code>(0), expected_body,
                                    expected_headers, HeaderValueOptionVector{}, grpc_status);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_error")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzErrorResponseWithAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an error response with no attributes is received.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationErrorNoAttributes) {
  initialize();

  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Internal;
  auto check_response = TestCommon::makeErrorCheckResponse(
      grpc_status, envoy::type::v3::InternalServerError, "", HeaderValueOptionVector{});
  auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Error, Http::Code::InternalServerError, "", HeaderValueOptionVector{},
      HeaderValueOptionVector{}, grpc_status);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_error")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzErrorResponseWithAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an OK response is received with dynamic metadata in that OK response.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithDynamicMetadata) {
  initialize();

  auto check_response = CheckResponsePtr();
  auto status = check_response->mutable_status();

  Protobuf::Struct expected_dynamic_metadata;
  auto* metadata_fields = expected_dynamic_metadata.mutable_fields();
  (*metadata_fields)["original"] = ValueUtil::stringValue("true");
  check_response->mutable_dynamic_metadata()->MergeFrom(expected_dynamic_metadata);

  Protobuf::Struct overridden_dynamic_metadata;
  metadata_fields = overridden_dynamic_metadata.mutable_fields();
  (*metadata_fields)["original"] = ValueUtil::stringValue("false");

  check_response->mutable_ok_response()->mutable_dynamic_metadata()->MergeFrom(
      overridden_dynamic_metadata);

  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
  status->set_code(grpc_status);

  // This is the expected authz response.
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;
  authz_response.dynamic_metadata = overridden_dynamic_metadata;
  authz_response.grpc_status = grpc_status;

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

  auto check_response = CheckResponsePtr();
  auto status = check_response->mutable_status();

  const auto grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok;
  status->set_code(grpc_status);

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
  authz_response.grpc_status = grpc_status;

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
  - header:
      key: invalid-append-action
      value: invalid-append-action-value
    append_action: 404
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
      .saw_invalid_append_actions = true,
      .status_code = Http::Code::OK,
      .grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok,
  };

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzOkResponse(expected_authz_response))));
  client_->onSuccess(CheckResponsePtr(std::move(check_response)), span_);
}

TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkUpstreamHeaderMutations) {
  initialize();

  envoy::service::auth::v3::CheckResponse check_response;
  TestUtility::loadFromYaml(R"EOF(
status:
  code: 0
ok_response:
  headers:
  - header:
      key: overwrite-header
      value: overwrite-value
  - header:
      key: append-header
      value: append-value
    append:
      value: true
  - header:
      key: explicit-no-append
      value: explicit-no-append-value
    append:
      value: false
)EOF",
                            check_response);

  // overwrite-header: append not set, defaults to false -> headers_to_set
  auto expected_authz_response = Response{
      .status = CheckStatus::OK,
      .headers_to_append = UnsafeHeaderVector{{"append-header", "append-value"}},
      .headers_to_set = UnsafeHeaderVector{{"overwrite-header", "overwrite-value"},
                                           {"explicit-no-append", "explicit-no-append-value"}},
      .status_code = Http::Code::OK,
      .grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok,
  };

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzOkResponse(expected_authz_response))));
  client_->onSuccess(CheckResponsePtr(std::move(check_response)), span_);
}

// TODO(https://github.com/envoyproxy/envoy/issues/45003)
// This test ensures the default (buggy) behavior remains unchanged since
// existing Envoy deployments may be relying on the default (buggy) behavior
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithKeepEmptyValueIgnored) {
  initialize();

  envoy::service::auth::v3::CheckResponse check_response;
  TestUtility::loadFromYaml(R"EOF(
status:
  code: 0
ok_response:
  headers:
  - header:
      key: x-keep-empty
      value: ""
    keep_empty_value: false
)EOF",
                            check_response);

  // If keep_empty_value were respected, the header "x-keep-empty" should be DROPPED
  // because its value is empty. However, the current implementation ignores this
  // flag and blindly adds the empty header to the headers_to_set vector.
  auto expected_authz_response = Response{
      .status = CheckStatus::OK,
      .headers_to_set = UnsafeHeaderVector{{"x-keep-empty", ""}},
      .status_code = Http::Code::OK,
      .grpc_status = Grpc::Status::WellKnownGrpcStatus::Ok,
  };

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzOkResponse(expected_authz_response))));
  client_->onSuccess(CheckResponsePtr(std::move(check_response)), span_);
}

TEST_F(ExtAuthzGrpcClientTest, TestGrpcClientResetOnComplete) {
  initialize();

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(span_, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(request_callbacks_, onComplete_(_)).WillOnce([this](ResponsePtr&) {
    // Synchronously destroy the client while inside onComplete()!
    client_.reset();
  });

  client_->onSuccess(std::make_unique<envoy::service::auth::v3::CheckResponse>(), span_);
}

TEST_F(ExtAuthzGrpcClientTest, EmitClientSpanDefault) {
  initialize();
  EXPECT_TRUE(client_->emitClientSpan());

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(*async_client_,
              sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
      .WillOnce([this](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                       Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
        EXPECT_EQ(std::nullopt, options.sampled_);
        return &async_request_;
      });

  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);
  EXPECT_CALL(request_callbacks_, onComplete_(_));
  client_->onSuccess(CheckResponsePtr(std::make_unique<envoy::service::auth::v3::CheckResponse>()),
                     Tracing::NullSpan::instance());
}

TEST_F(ExtAuthzGrpcClientTest, EmitClientSpanDisabled) {
  client_ = std::make_unique<GrpcClientImpl>(Grpc::RawAsyncClientPtr{async_client_}, timeout_,
                                             /*emit_client_span=*/false);
  EXPECT_FALSE(client_->emitClientSpan());

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(*async_client_,
              sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
      .WillOnce([this](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                       Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
        EXPECT_EQ(std::make_optional(false), options.sampled_);
        return &async_request_;
      });

  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);
  EXPECT_CALL(request_callbacks_, onComplete_(_));
  client_->onSuccess(CheckResponsePtr(std::make_unique<envoy::service::auth::v3::CheckResponse>()),
                     Tracing::NullSpan::instance());
}

TEST_F(ExtAuthzGrpcClientTest, SetEmitClientSpan) {
  initialize();
  EXPECT_TRUE(client_->emitClientSpan());

  client_->setEmitClientSpan(false);
  EXPECT_FALSE(client_->emitClientSpan());

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(*async_client_,
              sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
      .WillOnce([this](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                       Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
        EXPECT_EQ(std::make_optional(false), options.sampled_);
        return &async_request_;
      });

  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);
  EXPECT_CALL(request_callbacks_, onComplete_(_));
  client_->onSuccess(CheckResponsePtr(std::make_unique<envoy::service::auth::v3::CheckResponse>()),
                     Tracing::NullSpan::instance());
}

// Test the client when an ok response uses raw_value with fallback to value.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationOkWithRawValue) {
  initializeWithRawHeaderValue();

  const std::string non_utf8_bytes = std::string("raw-\xc0\xc0", 6);
  const std::string raw_append_bytes = std::string("append-\xc3\x28", 9);

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  check_response->mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
  auto* ok_response = check_response->mutable_ok_response();

  auto add_header_with_append = [](auto* repeated_headers, absl::string_view key,
                                   absl::string_view value, absl::string_view raw_value,
                                   bool append) {
    auto* entry = repeated_headers->Add();
    entry->mutable_append()->set_value(append);
    entry->mutable_header()->set_key(key);
    if (!value.empty()) {
      entry->mutable_header()->set_value(value);
    }
    if (!raw_value.empty()) {
      entry->mutable_header()->set_raw_value(raw_value);
    }
  };

  auto add_header_with_action =
      [](auto* repeated_headers, absl::string_view key, absl::string_view value,
         absl::string_view raw_value,
         envoy::config::core::v3::HeaderValueOption::HeaderAppendAction action) {
        auto* entry = repeated_headers->Add();
        entry->set_append_action(action);
        entry->mutable_header()->set_key(key);
        if (!value.empty()) {
          entry->mutable_header()->set_value(value);
        }
        if (!raw_value.empty()) {
          entry->mutable_header()->set_raw_value(raw_value);
        }
      };

  // Upstream request headers: raw_value only, fallback to value, explicit empty raw_value,
  // both empty, and raw_value taking precedence.
  add_header_with_append(ok_response->mutable_headers(), "x-raw-set", "", non_utf8_bytes, false);
  add_header_with_append(ok_response->mutable_headers(), "x-raw-append", "", raw_append_bytes,
                         true);
  add_header_with_append(ok_response->mutable_headers(), "x-fallback-set", "legacy-set", "", false);
  add_header_with_append(ok_response->mutable_headers(), "x-both-set", "legacy-ignored",
                         "raw-preferred", false);

  auto* explicit_empty_raw = ok_response->add_headers();
  explicit_empty_raw->mutable_append()->set_value(false);
  explicit_empty_raw->mutable_header()->set_key("x-empty-raw-fallback");
  explicit_empty_raw->mutable_header()->set_value("explicit-empty-raw-fallback");
  explicit_empty_raw->mutable_header()->set_raw_value("");

  auto* both_empty = ok_response->add_headers();
  both_empty->mutable_append()->set_value(false);
  both_empty->mutable_header()->set_key("x-both-empty");
  both_empty->mutable_header()->set_value("");
  both_empty->mutable_header()->set_raw_value("");

  // Downstream response headers across deprecated append, both-set precedence, and all
  // append_action variants including an invalid append_action alongside raw_value.
  add_header_with_append(ok_response->mutable_response_headers_to_add(), "x-resp-add", "",
                         non_utf8_bytes, true);
  add_header_with_append(ok_response->mutable_response_headers_to_add(), "x-resp-both",
                         "legacy-resp-ignored", "resp-both-raw", true);
  add_header_with_append(ok_response->mutable_response_headers_to_add(), "x-resp-set", "",
                         "resp-set-raw", false);
  add_header_with_action(ok_response->mutable_response_headers_to_add(), "x-action-append", "",
                         raw_append_bytes, Router::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
  add_header_with_action(ok_response->mutable_response_headers_to_add(), "x-action-absent", "",
                         non_utf8_bytes, Router::HeaderValueOption::ADD_IF_ABSENT);
  add_header_with_action(ok_response->mutable_response_headers_to_add(), "x-action-overwrite", "",
                         "overwrite-raw", Router::HeaderValueOption::OVERWRITE_IF_EXISTS);
  add_header_with_action(ok_response->mutable_response_headers_to_add(), "x-action-overwrite-add",
                         "fallback-value", "",
                         Router::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  add_header_with_action(ok_response->mutable_response_headers_to_add(),
                         "x-action-overwrite-add-raw", "", non_utf8_bytes,
                         Router::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  add_header_with_action(
      ok_response->mutable_response_headers_to_add(), "x-action-invalid", "", non_utf8_bytes,
      static_cast<envoy::config::core::v3::HeaderValueOption::HeaderAppendAction>(404));

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(_)).WillOnce([&](ResponsePtr& response) {
    EXPECT_EQ(CheckStatus::OK, response->status);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, response->grpc_status);
    EXPECT_TRUE(response->saw_invalid_append_actions);
    EXPECT_TRUE(response->headers_to_add.empty());
    EXPECT_TRUE(response->headers_to_remove.empty());
    EXPECT_TRUE(response->query_parameters_to_set.empty());
    EXPECT_TRUE(response->query_parameters_to_remove.empty());
    EXPECT_TRUE(TestCommon::compareHeaderVector(
        response->headers_to_set, {{"x-raw-set", non_utf8_bytes},
                                   {"x-fallback-set", "legacy-set"},
                                   {"x-both-set", "raw-preferred"},
                                   {"x-empty-raw-fallback", "explicit-empty-raw-fallback"},
                                   {"x-both-empty", ""}}));
    EXPECT_TRUE(TestCommon::compareHeaderVector(response->headers_to_append,
                                                {{"x-raw-append", raw_append_bytes}}));
    EXPECT_TRUE(TestCommon::compareHeaderVector(response->response_headers_to_add,
                                                {{"x-resp-add", non_utf8_bytes},
                                                 {"x-resp-both", "resp-both-raw"},
                                                 {"x-action-append", raw_append_bytes}}));
    EXPECT_TRUE(TestCommon::compareHeaderVector(response->response_headers_to_set,
                                                {{"x-resp-set", "resp-set-raw"},
                                                 {"x-action-overwrite-add", "fallback-value"},
                                                 {"x-action-overwrite-add-raw", non_utf8_bytes}}));
    EXPECT_TRUE(TestCommon::compareHeaderVector(response->response_headers_to_add_if_absent,
                                                {{"x-action-absent", non_utf8_bytes}}));
    EXPECT_TRUE(TestCommon::compareHeaderVector(response->response_headers_to_overwrite_if_exists,
                                                {{"x-action-overwrite", "overwrite-raw"}}));
  });
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response uses raw_value with fallback to value.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationDeniedWithRawValue) {
  initializeWithRawHeaderValue();

  const std::string non_utf8_bytes = std::string("denied-\xc0\xc0", 9);
  const std::string raw_append_bytes = std::string("denied-append-\xc3\x28", 16);

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  check_response->mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  auto* denied_response = check_response->mutable_denied_response();
  denied_response->mutable_status()->set_code(envoy::type::v3::Unauthorized);

  auto* raw_header = denied_response->add_headers();
  raw_header->mutable_header()->set_key("x-denied-raw");
  raw_header->mutable_header()->set_raw_value(non_utf8_bytes);

  auto* fallback_header = denied_response->add_headers();
  fallback_header->mutable_header()->set_key("x-denied-fallback");
  fallback_header->mutable_header()->set_value("legacy-denied");

  auto* both_header = denied_response->add_headers();
  both_header->mutable_header()->set_key("x-denied-both");
  both_header->mutable_header()->set_value("legacy-ignored");
  both_header->mutable_header()->set_raw_value("raw-preferred");

  auto* append_raw_header = denied_response->add_headers();
  append_raw_header->mutable_append()->set_value(true);
  append_raw_header->mutable_header()->set_key("x-denied-append-raw");
  append_raw_header->mutable_header()->set_raw_value(raw_append_bytes);

  auto* append_fallback_header = denied_response->add_headers();
  append_fallback_header->mutable_append()->set_value(true);
  append_fallback_header->mutable_header()->set_key("x-denied-append-fallback");
  append_fallback_header->mutable_header()->set_value("legacy-denied-append");

  auto* append_both_header = denied_response->add_headers();
  append_both_header->mutable_append()->set_value(true);
  append_both_header->mutable_header()->set_key("x-denied-append-both");
  append_both_header->mutable_header()->set_value("legacy-append-ignored");
  append_both_header->mutable_header()->set_raw_value("raw-append-preferred");

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_, onComplete_(_)).WillOnce([&](ResponsePtr& response) {
    EXPECT_EQ(CheckStatus::Denied, response->status);
    EXPECT_EQ(Http::Code::Unauthorized, response->status_code);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::PermissionDenied, response->grpc_status);
    EXPECT_TRUE(response->headers_to_add.empty());
    EXPECT_TRUE(TestCommon::compareHeaderVector(response->headers_to_set,
                                                {{"x-denied-raw", non_utf8_bytes},
                                                 {"x-denied-fallback", "legacy-denied"},
                                                 {"x-denied-both", "raw-preferred"}}));
    EXPECT_TRUE(TestCommon::compareHeaderVector(
        response->headers_to_append, {{"x-denied-append-raw", raw_append_bytes},
                                      {"x-denied-append-fallback", "legacy-denied-append"},
                                      {"x-denied-append-both", "raw-append-preferred"}}));
  });
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an error response uses raw_value with fallback to value.
TEST_F(ExtAuthzGrpcClientTest, AuthorizationErrorWithRawValue) {
  initializeWithRawHeaderValue();

  const std::string non_utf8_bytes = std::string("error-\xc0\xc0", 8);

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  check_response->mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
  auto* error_response = check_response->mutable_error_response();
  error_response->mutable_status()->set_code(envoy::type::v3::InternalServerError);

  auto* raw_header = error_response->add_headers();
  raw_header->mutable_header()->set_key("x-error-raw");
  raw_header->mutable_header()->set_raw_value(non_utf8_bytes);

  auto* both_set_header = error_response->add_headers();
  both_set_header->mutable_append()->set_value(false);
  both_set_header->mutable_header()->set_key("x-error-both-set");
  both_set_header->mutable_header()->set_value("legacy-set-ignored");
  both_set_header->mutable_header()->set_raw_value("raw-set-preferred");

  auto* append_raw_header = error_response->add_headers();
  append_raw_header->mutable_append()->set_value(true);
  append_raw_header->mutable_header()->set_key("x-error-append-raw");
  append_raw_header->mutable_header()->set_raw_value("append-raw");

  auto* both_append_header = error_response->add_headers();
  both_append_header->mutable_append()->set_value(true);
  both_append_header->mutable_header()->set_key("x-error-both-append");
  both_append_header->mutable_header()->set_value("legacy-append-ignored");
  both_append_header->mutable_header()->set_raw_value("raw-append-preferred");

  auto* fallback_header = error_response->add_headers();
  fallback_header->mutable_header()->set_key("x-error-fallback");
  fallback_header->mutable_header()->set_value("legacy-error");

  auto* append_fallback_header = error_response->add_headers();
  append_fallback_header->mutable_append()->set_value(true);
  append_fallback_header->mutable_header()->set_key("x-error-append-fallback");
  append_fallback_header->mutable_header()->set_value("legacy-error-append");

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_error")));
  EXPECT_CALL(request_callbacks_, onComplete_(_)).WillOnce([&](ResponsePtr& response) {
    EXPECT_EQ(CheckStatus::Error, response->status);
    EXPECT_EQ(Http::Code::InternalServerError, response->status_code);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Internal, response->grpc_status);
    EXPECT_TRUE(TestCommon::compareHeaderVector(response->headers_to_set,
                                                {{"x-error-raw", non_utf8_bytes},
                                                 {"x-error-both-set", "raw-set-preferred"},
                                                 {"x-error-fallback", "legacy-error"}}));
    EXPECT_TRUE(TestCommon::compareHeaderVector(
        response->headers_to_append, {{"x-error-append-raw", "append-raw"},
                                      {"x-error-both-append", "raw-append-preferred"},
                                      {"x-error-append-fallback", "legacy-error-append"}}));
  });
  client_->onSuccess(std::move(check_response), span_);
}

// Test that by default (read_raw_header_value = false), raw_value is ignored and only value is
// read across OK, denied, and error responses.
TEST_F(ExtAuthzGrpcClientTest, ReadRawHeaderValueDisabledByDefault) {
  initialize();

  // OK response: only value is read across headers, headers_to_append, and response_headers_to_add.
  {
    auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
    check_response->mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);
    auto* ok_response = check_response->mutable_ok_response();

    auto* raw_only = ok_response->add_headers();
    raw_only->mutable_header()->set_key("x-raw-only");
    raw_only->mutable_header()->set_raw_value("ignored-raw");

    auto* both = ok_response->add_headers();
    both->mutable_header()->set_key("x-both");
    both->mutable_header()->set_value("legacy-value");
    both->mutable_header()->set_raw_value("ignored-raw");

    auto* append_raw_only = ok_response->add_headers();
    append_raw_only->mutable_append()->set_value(true);
    append_raw_only->mutable_header()->set_key("x-append-raw-only");
    append_raw_only->mutable_header()->set_raw_value("ignored-raw");

    auto* append_both = ok_response->add_headers();
    append_both->mutable_append()->set_value(true);
    append_both->mutable_header()->set_key("x-append-both");
    append_both->mutable_header()->set_value("legacy-append");
    append_both->mutable_header()->set_raw_value("ignored-raw");

    auto* resp_has_append_true = ok_response->add_response_headers_to_add();
    resp_has_append_true->mutable_append()->set_value(true);
    resp_has_append_true->mutable_header()->set_key("x-resp-add");
    resp_has_append_true->mutable_header()->set_value("legacy-resp-add");
    resp_has_append_true->mutable_header()->set_raw_value("ignored-raw");

    auto* resp_has_append_false = ok_response->add_response_headers_to_add();
    resp_has_append_false->mutable_append()->set_value(false);
    resp_has_append_false->mutable_header()->set_key("x-resp-set");
    resp_has_append_false->mutable_header()->set_value("legacy-resp-set");
    resp_has_append_false->mutable_header()->set_raw_value("ignored-raw");

    auto* resp_action_append = ok_response->add_response_headers_to_add();
    resp_action_append->set_append_action(Router::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
    resp_action_append->mutable_header()->set_key("x-resp-action-append");
    resp_action_append->mutable_header()->set_value("legacy-action-append");
    resp_action_append->mutable_header()->set_raw_value("ignored-raw");

    auto* resp_action_absent = ok_response->add_response_headers_to_add();
    resp_action_absent->set_append_action(Router::HeaderValueOption::ADD_IF_ABSENT);
    resp_action_absent->mutable_header()->set_key("x-resp-absent");
    resp_action_absent->mutable_header()->set_value("legacy-absent");
    resp_action_absent->mutable_header()->set_raw_value("ignored-raw");

    auto* resp_action_overwrite = ok_response->add_response_headers_to_add();
    resp_action_overwrite->set_append_action(Router::HeaderValueOption::OVERWRITE_IF_EXISTS);
    resp_action_overwrite->mutable_header()->set_key("x-resp-overwrite");
    resp_action_overwrite->mutable_header()->set_value("legacy-overwrite");
    resp_action_overwrite->mutable_header()->set_raw_value("ignored-raw");

    auto* resp_action_overwrite_add = ok_response->add_response_headers_to_add();
    resp_action_overwrite_add->set_append_action(
        Router::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
    resp_action_overwrite_add->mutable_header()->set_key("x-resp-overwrite-add");
    resp_action_overwrite_add->mutable_header()->set_raw_value("ignored-raw");

    envoy::service::auth::v3::CheckRequest request;
    expectCallSend(request);
    client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

    EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
    EXPECT_CALL(request_callbacks_, onComplete_(_)).WillOnce([&](ResponsePtr& response) {
      EXPECT_EQ(CheckStatus::OK, response->status);
      EXPECT_EQ(static_cast<Http::Code>(0), response->status_code);
      EXPECT_FALSE(response->saw_invalid_append_actions);
      EXPECT_TRUE(TestCommon::compareHeaderVector(
          response->headers_to_set, {{"x-raw-only", ""}, {"x-both", "legacy-value"}}));
      EXPECT_TRUE(TestCommon::compareHeaderVector(
          response->headers_to_append,
          {{"x-append-raw-only", ""}, {"x-append-both", "legacy-append"}}));
      EXPECT_TRUE(TestCommon::compareHeaderVector(
          response->response_headers_to_add,
          {{"x-resp-add", "legacy-resp-add"}, {"x-resp-action-append", "legacy-action-append"}}));
      EXPECT_TRUE(TestCommon::compareHeaderVector(
          response->response_headers_to_set,
          {{"x-resp-set", "legacy-resp-set"}, {"x-resp-overwrite-add", ""}}));
      EXPECT_TRUE(TestCommon::compareHeaderVector(response->response_headers_to_add_if_absent,
                                                  {{"x-resp-absent", "legacy-absent"}}));
      EXPECT_TRUE(TestCommon::compareHeaderVector(response->response_headers_to_overwrite_if_exists,
                                                  {{"x-resp-overwrite", "legacy-overwrite"}}));
    });
    client_->onSuccess(std::move(check_response), span_);
  }

  // Denied response: only value is read when read_raw_header_value is false.
  {
    auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
    check_response->mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
    auto* denied_response = check_response->mutable_denied_response();
    denied_response->mutable_status()->set_code(envoy::type::v3::Unauthorized);

    auto* denied_raw_only = denied_response->add_headers();
    denied_raw_only->mutable_header()->set_key("x-denied-raw-only");
    denied_raw_only->mutable_header()->set_raw_value("ignored-raw");

    auto* denied_both = denied_response->add_headers();
    denied_both->mutable_header()->set_key("x-denied-both");
    denied_both->mutable_header()->set_value("legacy-denied");
    denied_both->mutable_header()->set_raw_value("ignored-raw");

    auto* denied_append = denied_response->add_headers();
    denied_append->mutable_append()->set_value(true);
    denied_append->mutable_header()->set_key("x-denied-append");
    denied_append->mutable_header()->set_value("legacy-denied-append");
    denied_append->mutable_header()->set_raw_value("ignored-raw");

    envoy::service::auth::v3::CheckRequest request;
    expectCallSend(request);
    client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

    EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
    EXPECT_CALL(request_callbacks_, onComplete_(_)).WillOnce([&](ResponsePtr& response) {
      EXPECT_EQ(CheckStatus::Denied, response->status);
      EXPECT_EQ(Http::Code::Unauthorized, response->status_code);
      EXPECT_TRUE(TestCommon::compareHeaderVector(
          response->headers_to_set,
          {{"x-denied-raw-only", ""}, {"x-denied-both", "legacy-denied"}}));
      EXPECT_TRUE(TestCommon::compareHeaderVector(response->headers_to_append,
                                                  {{"x-denied-append", "legacy-denied-append"}}));
    });
    client_->onSuccess(std::move(check_response), span_);
  }

  // Error response: only value is read when read_raw_header_value is false.
  {
    auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
    check_response->mutable_status()->set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
    auto* error_response = check_response->mutable_error_response();
    error_response->mutable_status()->set_code(envoy::type::v3::ServiceUnavailable);

    auto* error_raw_only = error_response->add_headers();
    error_raw_only->mutable_header()->set_key("x-error-raw-only");
    error_raw_only->mutable_header()->set_raw_value("ignored-raw");

    auto* error_both = error_response->add_headers();
    error_both->mutable_header()->set_key("x-error-both");
    error_both->mutable_header()->set_value("legacy-error");
    error_both->mutable_header()->set_raw_value("ignored-raw");

    auto* error_append = error_response->add_headers();
    error_append->mutable_append()->set_value(true);
    error_append->mutable_header()->set_key("x-error-append");
    error_append->mutable_header()->set_value("legacy-error-append");
    error_append->mutable_header()->set_raw_value("ignored-raw");

    envoy::service::auth::v3::CheckRequest request;
    expectCallSend(request);
    client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

    EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_error")));
    EXPECT_CALL(request_callbacks_, onComplete_(_)).WillOnce([&](ResponsePtr& response) {
      EXPECT_EQ(CheckStatus::Error, response->status);
      EXPECT_EQ(Http::Code::ServiceUnavailable, response->status_code);
      EXPECT_TRUE(TestCommon::compareHeaderVector(
          response->headers_to_set, {{"x-error-raw-only", ""}, {"x-error-both", "legacy-error"}}));
      EXPECT_TRUE(TestCommon::compareHeaderVector(response->headers_to_append,
                                                  {{"x-error-append", "legacy-error-append"}}));
    });
    client_->onSuccess(std::move(check_response), span_);
  }
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
