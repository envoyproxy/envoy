#include "envoy/http/header_validator_errors.h"

#include "source/common/http/header_validation_utility.h"

#include "test/mocks/http/header_validator.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace HeaderValidationUtility {
namespace {

#ifdef ENVOY_ENABLE_UHV

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

class HeaderValidationUtilityTest : public ::testing::Test {
protected:
  void expectValidationSuccess() {
    EXPECT_CALL(validator_, validateRequestHeaders(_))
        .WillOnce(Return(HeaderValidator::ValidationResult::success()));
  }
  void expectValidationFailure(absl::string_view details) {
    EXPECT_CALL(validator_, validateRequestHeaders(_))
        .WillOnce(Return(HeaderValidator::ValidationResult(
            HeaderValidator::ValidationResult::Action::Reject, details)));
  }
  void expectTransformation(ServerHeaderValidator::RequestHeadersTransformationResult result) {
    EXPECT_CALL(validator_, transformRequestHeaders(_)).WillOnce(Return(result));
  }

  StrictMock<MockServerHeaderValidator> validator_;
  TestRequestHeaderMapImpl headers_{
      {":authority", "host"}, {":path", "/"}, {":method", "GET"}, {":scheme", "https"}};
  TestRequestTrailerMapImpl trailers_{{"trailer", "value"}};
};

TEST_F(HeaderValidationUtilityTest, HeadersAccepted) {
  expectValidationSuccess();
  expectTransformation(ServerHeaderValidator::RequestHeadersTransformationResult::success());
  EXPECT_FALSE(checkRequestHeaders(validator_, headers_, Protocol::Http11, false).has_value());
}

TEST_F(HeaderValidationUtilityTest, HeadersValidationFailure) {
  expectValidationFailure("bad_headers");
  auto rejection = checkRequestHeaders(validator_, headers_, Protocol::Http11,
                                       /*stream_error_on_invalid_http_message=*/false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Phase::Headers, rejection->phase);
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
  EXPECT_EQ(Code::BadRequest, rejection->response_code);
  EXPECT_EQ("bad_headers", rejection->details);
  EXPECT_FALSE(rejection->transformation_failed);
  // Without stream errors the connection is closed on validation failures.
  EXPECT_TRUE(rejection->close_connection);
}

TEST_F(HeaderValidationUtilityTest, HeadersValidationFailureWithStreamError) {
  expectValidationFailure("bad_headers");
  auto rejection = checkRequestHeaders(validator_, headers_, Protocol::Http2,
                                       /*stream_error_on_invalid_http_message=*/true);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
  EXPECT_FALSE(rejection->close_connection);
}

TEST_F(HeaderValidationUtilityTest, HeadersTransformationFailure) {
  expectValidationSuccess();
  expectTransformation(ServerHeaderValidator::RequestHeadersTransformationResult(
      ServerHeaderValidator::RequestHeadersTransformationResult::Action::Reject, "bad_path"));
  auto rejection = checkRequestHeaders(validator_, headers_, Protocol::Http11, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
  EXPECT_EQ(Code::BadRequest, rejection->response_code);
  EXPECT_EQ("bad_path", rejection->details);
  EXPECT_TRUE(rejection->transformation_failed);
  EXPECT_TRUE(rejection->close_connection);
}

TEST_F(HeaderValidationUtilityTest, HeadersRedirect) {
  expectValidationSuccess();
  expectTransformation(ServerHeaderValidator::RequestHeadersTransformationResult(
      ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect,
      UhvResponseCodeDetail::get().EscapedSlashesInPath));
  auto rejection = checkRequestHeaders(validator_, headers_, Protocol::Http11, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::Redirect, rejection->action);
  EXPECT_EQ(UhvResponseCodeDetail::get().EscapedSlashesInPath, rejection->details);
  EXPECT_TRUE(rejection->transformation_failed);
  // Escaped slashes handling never closes the connection (pre-UHV HCM behavior).
  EXPECT_FALSE(rejection->close_connection);
}

// Failures that the pre-UHV HCM handled with a 400 only (not closing the connection).
TEST_F(HeaderValidationUtilityTest, StreamErrorOnlyErrors) {
  EXPECT_TRUE(streamErrorOnlyErrors(UhvResponseCodeDetail::get().FragmentInUrlPath));
  EXPECT_TRUE(streamErrorOnlyErrors(UhvResponseCodeDetail::get().EscapedSlashesInPath));
  EXPECT_TRUE(streamErrorOnlyErrors(UhvResponseCodeDetail::get().Percent00InPath));
  EXPECT_FALSE(streamErrorOnlyErrors(UhvResponseCodeDetail::get().InvalidUnderscore));

  expectValidationFailure(UhvResponseCodeDetail::get().FragmentInUrlPath);
  auto rejection = checkRequestHeaders(validator_, headers_, Protocol::Http11, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
  EXPECT_FALSE(rejection->close_connection);
}

TEST_F(HeaderValidationUtilityTest, InvalidTransferEncodingIsNotImplemented) {
  expectValidationFailure(Http1ResponseCodeDetail::get().InvalidTransferEncoding);
  auto rejection = checkRequestHeaders(validator_, headers_, Protocol::Http11, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
  EXPECT_EQ(Code::NotImplemented, rejection->response_code);
}

// The H/2 codec resets requests with underscores in header names instead of sending 400.
TEST_F(HeaderValidationUtilityTest, Http2UnderscoreResetsStream) {
  expectValidationFailure(UhvResponseCodeDetail::get().InvalidUnderscore);
  auto rejection = checkRequestHeaders(validator_, headers_, Protocol::Http2, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::ResetStream, rejection->action);
  EXPECT_EQ(UhvResponseCodeDetail::get().InvalidUnderscore, rejection->details);
  EXPECT_FALSE(rejection->close_connection);

  // Other protocols send a local reply.
  expectValidationFailure(UhvResponseCodeDetail::get().InvalidUnderscore);
  rejection = checkRequestHeaders(validator_, headers_, Protocol::Http3, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
}

TEST_F(HeaderValidationUtilityTest, TrailersAccepted) {
  EXPECT_CALL(validator_, validateRequestTrailers(_))
      .WillOnce(Return(HeaderValidator::ValidationResult::success()));
  EXPECT_CALL(validator_, transformRequestTrailers(_))
      .WillOnce(Return(HeaderValidator::TransformationResult::success()));
  EXPECT_FALSE(checkRequestTrailers(validator_, trailers_, Protocol::Http11, false).has_value());
}

TEST_F(HeaderValidationUtilityTest, TrailersValidationFailure) {
  EXPECT_CALL(validator_, validateRequestTrailers(_))
      .WillOnce(Return(HeaderValidator::ValidationResult(
          HeaderValidator::ValidationResult::Action::Reject, "bad_trailers")));
  // H/1 sends a local reply.
  auto rejection = checkRequestTrailers(validator_, trailers_, Protocol::Http11, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Phase::Trailers, rejection->phase);
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
  EXPECT_EQ(Code::BadRequest, rejection->response_code);
  EXPECT_EQ("bad_trailers", rejection->details);
  EXPECT_TRUE(rejection->close_connection);

  // H/2 and H/3 reset the stream.
  EXPECT_CALL(validator_, validateRequestTrailers(_))
      .WillOnce(Return(HeaderValidator::ValidationResult(
          HeaderValidator::ValidationResult::Action::Reject, "bad_trailers")));
  rejection = checkRequestTrailers(validator_, trailers_, Protocol::Http2,
                                   /*stream_error_on_invalid_http_message=*/true);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::ResetStream, rejection->action);
  EXPECT_FALSE(rejection->close_connection);
}

TEST_F(HeaderValidationUtilityTest, TrailersTransformationFailure) {
  EXPECT_CALL(validator_, validateRequestTrailers(_))
      .WillOnce(Return(HeaderValidator::ValidationResult::success()));
  EXPECT_CALL(validator_, transformRequestTrailers(_))
      .WillOnce(Return(HeaderValidator::TransformationResult(
          HeaderValidator::TransformationResult::Action::Reject, "bad_trailers")));
  auto rejection = checkRequestTrailers(validator_, trailers_, Protocol::Http11, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::LocalReply, rejection->action);
  EXPECT_EQ("bad_trailers", rejection->details);
}

TEST_F(HeaderValidationUtilityTest, Http2UnderscoreInTrailersResetsStream) {
  EXPECT_CALL(validator_, validateRequestTrailers(_))
      .WillOnce(Return(
          HeaderValidator::ValidationResult(HeaderValidator::ValidationResult::Action::Reject,
                                            UhvResponseCodeDetail::get().InvalidUnderscore)));
  auto rejection = checkRequestTrailers(validator_, trailers_, Protocol::Http2, false);
  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(RequestRejection::Action::ResetStream, rejection->action);
  EXPECT_FALSE(rejection->close_connection);
}

#endif // ENVOY_ENABLE_UHV

} // namespace
} // namespace HeaderValidationUtility
} // namespace Http
} // namespace Envoy
