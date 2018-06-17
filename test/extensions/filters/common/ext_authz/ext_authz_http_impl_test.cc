#include "envoy/api/v2/core/base.pb.h"

#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
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

typedef std::vector<envoy::api::v2::core::HeaderValueOption> HeaderValueOptionVector;

class ExtAuthzHttpClientTest : public testing::Test {
public:
  ExtAuthzHttpClientTest()
      : cluster_name_{"foo"}, cluster_manager_{}, timeout_{}, path_prefix_{"/bar"},
        response_headers_to_remove_{}, async_client_{}, async_request_{&async_client_},
        client_(cluster_name_, cluster_manager_, timeout_, path_prefix_,
                response_headers_to_remove_) {
    ON_CALL(cluster_manager_, httpAsyncClientForCluster(cluster_name_))
        .WillByDefault(ReturnRef(async_client_));
  }

  std::string cluster_name_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  MockRequestCallbacks request_callbacks_;
  absl::optional<std::chrono::milliseconds> timeout_;
  std::string path_prefix_;
  std::vector<Http::LowerCaseString> response_headers_to_remove_;
  NiceMock<Http::MockAsyncClient> async_client_;
  NiceMock<Http::MockAsyncClientRequest> async_request_;
  RawHttpClientImpl client_;
};

TEST_F(ExtAuthzHttpClientTest, BasicOK) {
  const auto expected_headers = TestCommon::makeHeaderValueOption(":status", "200", false);
  const auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::OK);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_.onSuccess(std::move(check_response));
}

TEST_F(ExtAuthzHttpClientTest, BasicDenied) {
  const auto expected_headers = TestCommon::makeHeaderValueOption(":status", "403", false);
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Forbidden, "", expected_headers);
  auto check_response = TestCommon::makeMessageResponse(expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(std::move(check_response));
}

TEST_F(ExtAuthzHttpClientTest, AuthorizationDeniedWithAllAttributes) {
  const auto expected_body = std::string{"test"};
  const auto expected_headers = TestCommon::makeHeaderValueOption(":status", "401", false);
  const auto authz_response = TestCommon::makeAuthzResponse(
      CheckStatus::Denied, Http::Code::Unauthorized, expected_body, expected_headers);
  auto check_response = TestCommon::makeMessageResponse(expected_headers, expected_body);

  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(std::move(check_response));
}

TEST_F(ExtAuthzHttpClientTest, BasicError) {
  envoy::service::auth::v2alpha::CheckRequest request;
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_.onFailure(Http::AsyncClient::FailureReason::Reset);
}

TEST_F(ExtAuthzHttpClientTest, Cancel) {
  envoy::service::auth::v2alpha::CheckRequest request;

  EXPECT_CALL(async_client_, send_(_, _, _)).WillOnce(Return(&async_request_));
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
