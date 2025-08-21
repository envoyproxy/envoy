#include "envoy/common/platform.h"

#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/global.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GrpcContextTest, GetGrpcStatus) {
  Http::TestResponseHeaderMapImpl ok_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Status::Ok, Common::getGrpcStatus(ok_trailers).value());

  Http::TestResponseHeaderMapImpl no_status_trailers{{"foo", "bar"}};
  EXPECT_FALSE(Common::getGrpcStatus(no_status_trailers));

  Http::TestResponseHeaderMapImpl aborted_trailers{{"grpc-status", "10"}};
  EXPECT_EQ(Status::Aborted, Common::getGrpcStatus(aborted_trailers).value());

  Http::TestResponseHeaderMapImpl unauth_trailers{{"grpc-status", "16"}};
  EXPECT_EQ(Status::Unauthenticated, Common::getGrpcStatus(unauth_trailers).value());

  Http::TestResponseHeaderMapImpl invalid_trailers{{"grpc-status", "-1"}};
  EXPECT_EQ(Status::InvalidCode, Common::getGrpcStatus(invalid_trailers).value());

  Http::TestResponseHeaderMapImpl user_defined_invalid_trailers{{"grpc-status", "1024"}};
  EXPECT_EQ(Status::InvalidCode, Common::getGrpcStatus(invalid_trailers).value());

  Http::TestResponseHeaderMapImpl user_defined_trailers{{"grpc-status", "1024"}};
  EXPECT_EQ(1024, Common::getGrpcStatus(user_defined_trailers, true).value());
}

TEST(GrpcContextTest, GetGrpcStatusWithFallbacks) {
  Http::TestResponseHeaderMapImpl ok_status_headers{{"grpc-status", "0"}};
  Http::TestResponseHeaderMapImpl no_status_headers{{"foo", "bar"}};
  Http::TestResponseTrailerMapImpl ok_status_trailers{{"grpc-status", "0"}};
  Http::TestResponseTrailerMapImpl no_status_trailers{{"foo", "bar"}};
  NiceMock<StreamInfo::MockStreamInfo> info;
  EXPECT_CALL(info, responseCode()).WillRepeatedly(testing::Return(404));

  EXPECT_EQ(Status::Ok, Common::getGrpcStatus(ok_status_trailers, no_status_headers, info).value());

  EXPECT_EQ(Status::Ok, Common::getGrpcStatus(no_status_trailers, ok_status_headers, info).value());

  EXPECT_EQ(Status::Unimplemented,
            Common::getGrpcStatus(no_status_trailers, no_status_headers, info).value());

  NiceMock<StreamInfo::MockStreamInfo> info_without_code;
  EXPECT_FALSE(Common::getGrpcStatus(no_status_trailers, no_status_headers, info_without_code));
}

TEST(GrpcContextTest, GetGrpcMessage) {
  Http::TestResponseTrailerMapImpl empty_trailers;
  EXPECT_EQ("", Common::getGrpcMessage(empty_trailers));

  Http::TestResponseTrailerMapImpl error_trailers{{"grpc-message", "Some error"}};
  EXPECT_EQ("Some error", Common::getGrpcMessage(error_trailers));

  Http::TestResponseTrailerMapImpl empty_error_trailers{{"grpc-message", ""}};
  EXPECT_EQ("", Common::getGrpcMessage(empty_error_trailers));
}

TEST(GrpcContextTest, GetGrpcTimeout) {
  Http::TestRequestHeaderMapImpl empty_headers;
  EXPECT_EQ(absl::nullopt, Common::getGrpcTimeout(empty_headers));

  Http::TestRequestHeaderMapImpl empty_grpc_timeout{{"grpc-timeout", ""}};
  EXPECT_EQ(absl::nullopt, Common::getGrpcTimeout(empty_grpc_timeout));

  Http::TestRequestHeaderMapImpl missing_unit{{"grpc-timeout", "123"}};
  EXPECT_EQ(absl::nullopt, Common::getGrpcTimeout(missing_unit));

  Http::TestRequestHeaderMapImpl small_missing_unit{{"grpc-timeout", "1"}};
  EXPECT_EQ(absl::nullopt, Common::getGrpcTimeout(small_missing_unit));

  Http::TestRequestHeaderMapImpl illegal_unit{{"grpc-timeout", "123F"}};
  EXPECT_EQ(absl::nullopt, Common::getGrpcTimeout(illegal_unit));

  Http::TestRequestHeaderMapImpl unit_hours{{"grpc-timeout", "0H"}};
  EXPECT_EQ(std::chrono::milliseconds(0), Common::getGrpcTimeout(unit_hours));

  Http::TestRequestHeaderMapImpl zero_hours{{"grpc-timeout", "1H"}};
  EXPECT_EQ(std::chrono::milliseconds(60 * 60 * 1000), Common::getGrpcTimeout(zero_hours));

  Http::TestRequestHeaderMapImpl unit_minutes{{"grpc-timeout", "1M"}};
  EXPECT_EQ(std::chrono::milliseconds(60 * 1000), Common::getGrpcTimeout(unit_minutes));

  Http::TestRequestHeaderMapImpl unit_seconds{{"grpc-timeout", "1S"}};
  EXPECT_EQ(std::chrono::milliseconds(1000), Common::getGrpcTimeout(unit_seconds));

  Http::TestRequestHeaderMapImpl unit_milliseconds{{"grpc-timeout", "12345678m"}};
  EXPECT_EQ(std::chrono::milliseconds(12345678), Common::getGrpcTimeout(unit_milliseconds));

  Http::TestRequestHeaderMapImpl unit_microseconds{{"grpc-timeout", "1000001u"}};
  EXPECT_EQ(std::chrono::milliseconds(1001), Common::getGrpcTimeout(unit_microseconds));

  Http::TestRequestHeaderMapImpl unit_nanoseconds{{"grpc-timeout", "12345678n"}};
  EXPECT_EQ(std::chrono::milliseconds(13), Common::getGrpcTimeout(unit_nanoseconds));

  // Test max 8 digits to prevent millisecond overflow.
  Http::TestRequestHeaderMapImpl value_overflow{{"grpc-timeout", "6666666666666H"}};
  EXPECT_EQ(absl::nullopt, Common::getGrpcTimeout(value_overflow));

  // Reject negative values.
  Http::TestRequestHeaderMapImpl value_negative{{"grpc-timeout", "-1S"}};
  EXPECT_EQ(absl::nullopt, Common::getGrpcTimeout(value_negative));

  // Allow positive values marked with +.
  Http::TestRequestHeaderMapImpl value_positive{{"grpc-timeout", "+1S"}};
  EXPECT_EQ(std::chrono::milliseconds(1000), Common::getGrpcTimeout(value_positive));

  // No leading whitespace are not enforced on decode so we don't test for them.
}

TEST(GrpcCommonTest, GrpcStatusDetailsBin) {
  Http::TestResponseTrailerMapImpl empty_trailers;
  EXPECT_FALSE(Common::getGrpcStatusDetailsBin(empty_trailers));

  Http::TestResponseTrailerMapImpl invalid_value{{"grpc-status-details-bin", "invalid"}};
  EXPECT_FALSE(Common::getGrpcStatusDetailsBin(invalid_value));

  Http::TestResponseTrailerMapImpl unpadded_value{
      {"grpc-status-details-bin", "CAUSElJlc291cmNlIG5vdCBmb3VuZA"}};
  auto status = Common::getGrpcStatusDetailsBin(unpadded_value);
  ASSERT_TRUE(status);
  EXPECT_EQ(Status::WellKnownGrpcStatus::NotFound, status->code());
  EXPECT_EQ("Resource not found", status->message());

  Http::TestResponseTrailerMapImpl padded_value{
      {"grpc-status-details-bin", "CAUSElJlc291cmNlIG5vdCBmb3VuZA=="}};
  status = Common::getGrpcStatusDetailsBin(padded_value);
  ASSERT_TRUE(status);
  EXPECT_EQ(Status::WellKnownGrpcStatus::NotFound, status->code());
  EXPECT_EQ("Resource not found", status->message());
}

TEST(GrpcContextTest, ToGrpcTimeout) {
  Http::TestRequestHeaderMapImpl headers;

  Common::toGrpcTimeout(std::chrono::milliseconds(0UL), headers);
  EXPECT_EQ("0m", headers.getGrpcTimeoutValue());

  Common::toGrpcTimeout(std::chrono::milliseconds(1UL), headers);
  EXPECT_EQ("1m", headers.getGrpcTimeoutValue());

  Common::toGrpcTimeout(std::chrono::milliseconds(100000000UL), headers);
  EXPECT_EQ("100000S", headers.getGrpcTimeoutValue());

  Common::toGrpcTimeout(std::chrono::milliseconds(100000000000UL), headers);
  EXPECT_EQ("1666666M", headers.getGrpcTimeoutValue());

  Common::toGrpcTimeout(std::chrono::milliseconds(9000000000000UL), headers);
  EXPECT_EQ("2500000H", headers.getGrpcTimeoutValue());

  Common::toGrpcTimeout(std::chrono::milliseconds(360000000000000UL), headers);
  EXPECT_EQ("99999999H", headers.getGrpcTimeoutValue());

  Common::toGrpcTimeout(std::chrono::milliseconds(UINT64_MAX), headers);
  EXPECT_EQ("99999999H", headers.getGrpcTimeoutValue());
}

TEST(GrpcContextTest, PrepareHeaders) {
  {
    Http::RequestMessagePtr message =
        Common::prepareHeaders("cluster", "service_name", "method_name", absl::nullopt);

    EXPECT_EQ("POST", message->headers().getMethodValue());
    EXPECT_EQ("/service_name/method_name", message->headers().getPathValue());
    EXPECT_EQ("cluster", message->headers().getHostValue());
    EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());
  }
  {
    Http::RequestMessagePtr message = Common::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::milliseconds>(1));

    EXPECT_EQ("POST", message->headers().getMethodValue());
    EXPECT_EQ("/service_name/method_name", message->headers().getPathValue());
    EXPECT_EQ("cluster", message->headers().getHostValue());
    EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());
    EXPECT_EQ("1m", message->headers().getGrpcTimeoutValue());
  }
  {
    Http::RequestMessagePtr message = Common::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::seconds>(1));

    EXPECT_EQ("POST", message->headers().getMethodValue());
    EXPECT_EQ("/service_name/method_name", message->headers().getPathValue());
    EXPECT_EQ("cluster", message->headers().getHostValue());
    EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());
    EXPECT_EQ("1000m", message->headers().getGrpcTimeoutValue());
  }
  {
    Http::RequestMessagePtr message = Common::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::minutes>(1));

    EXPECT_EQ("POST", message->headers().getMethodValue());
    EXPECT_EQ("/service_name/method_name", message->headers().getPathValue());
    EXPECT_EQ("cluster", message->headers().getHostValue());
    EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());
    EXPECT_EQ("60000m", message->headers().getGrpcTimeoutValue());
  }
  {
    Http::RequestMessagePtr message = Common::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::hours>(1));

    EXPECT_EQ("POST", message->headers().getMethodValue());
    EXPECT_EQ("/service_name/method_name", message->headers().getPathValue());
    EXPECT_EQ("cluster", message->headers().getHostValue());
    EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());
    EXPECT_EQ("3600000m", message->headers().getGrpcTimeoutValue());
  }
  {
    Http::RequestMessagePtr message = Common::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::hours>(100000000));

    EXPECT_EQ("POST", message->headers().getMethodValue());
    EXPECT_EQ("/service_name/method_name", message->headers().getPathValue());
    EXPECT_EQ("cluster", message->headers().getHostValue());
    EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());
    EXPECT_EQ("99999999H", message->headers().getGrpcTimeoutValue());
  }
  {
    Http::RequestMessagePtr message =
        Common::prepareHeaders("cluster", "service_name", "method_name",
                               absl::optional<std::chrono::milliseconds>(100000000000));

    EXPECT_EQ("POST", message->headers().getMethodValue());
    EXPECT_EQ("/service_name/method_name", message->headers().getPathValue());
    EXPECT_EQ("cluster", message->headers().getHostValue());
    EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());
    EXPECT_EQ("1666666M", message->headers().getGrpcTimeoutValue());
  }
}

TEST(GrpcContextTest, GrpcToHttpStatus) {
  const std::vector<std::pair<Status::GrpcStatus, uint64_t>> test_set = {
      {Status::WellKnownGrpcStatus::Ok, 200},
      {Status::WellKnownGrpcStatus::Canceled, 499},
      {Status::WellKnownGrpcStatus::Unknown, 500},
      {Status::WellKnownGrpcStatus::InvalidArgument, 400},
      {Status::WellKnownGrpcStatus::DeadlineExceeded, 504},
      {Status::WellKnownGrpcStatus::NotFound, 404},
      {Status::WellKnownGrpcStatus::AlreadyExists, 409},
      {Status::WellKnownGrpcStatus::PermissionDenied, 403},
      {Status::WellKnownGrpcStatus::ResourceExhausted, 429},
      {Status::WellKnownGrpcStatus::FailedPrecondition, 400},
      {Status::WellKnownGrpcStatus::Aborted, 409},
      {Status::WellKnownGrpcStatus::OutOfRange, 400},
      {Status::WellKnownGrpcStatus::Unimplemented, 501},
      {Status::WellKnownGrpcStatus::Internal, 500},
      {Status::WellKnownGrpcStatus::Unavailable, 503},
      {Status::WellKnownGrpcStatus::DataLoss, 500},
      {Status::WellKnownGrpcStatus::Unauthenticated, 401},
      {Status::WellKnownGrpcStatus::InvalidCode, 500},
  };
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, Grpc::Utility::grpcToHttpStatus(test_case.first));
  }
}

TEST(GrpcContextTest, HttpToGrpcStatus) {
  const std::vector<std::pair<uint64_t, Status::GrpcStatus>> test_set = {
      {400, Status::WellKnownGrpcStatus::Internal},
      {401, Status::WellKnownGrpcStatus::Unauthenticated},
      {403, Status::WellKnownGrpcStatus::PermissionDenied},
      {404, Status::WellKnownGrpcStatus::Unimplemented},
      {429, Status::WellKnownGrpcStatus::Unavailable},
      {502, Status::WellKnownGrpcStatus::Unavailable},
      {503, Status::WellKnownGrpcStatus::Unavailable},
      {504, Status::WellKnownGrpcStatus::Unavailable},
      {500, Status::WellKnownGrpcStatus::Unknown},
  };
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, Grpc::Utility::httpToGrpcStatus(test_case.first));
  }
}

TEST(GrpcContextTest, HasGrpcContentType) {
  {
    Http::TestRequestHeaderMapImpl headers{};
    EXPECT_FALSE(Common::hasGrpcContentType(headers));
  }
  auto isGrpcContentType = [](const std::string& s) {
    Http::TestRequestHeaderMapImpl headers{{"content-type", s}};
    return Common::hasGrpcContentType(headers);
  };
  EXPECT_FALSE(isGrpcContentType(""));
  EXPECT_FALSE(isGrpcContentType("application/text"));
  EXPECT_TRUE(isGrpcContentType("application/grpc"));
  EXPECT_TRUE(isGrpcContentType("application/grpc+"));
  EXPECT_TRUE(isGrpcContentType("application/grpc+foo"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-web"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-web+foo"));
}

TEST(GrpcContextTest, IsGrpcRequestHeader) {
  Http::TestRequestHeaderMapImpl is{
      {":method", "GET"}, {":path", "/"}, {"content-type", "application/grpc"}};
  EXPECT_TRUE(Common::isGrpcRequestHeaders(is));
  Http::TestRequestHeaderMapImpl is_not{{":method", "CONNECT"},
                                        {"content-type", "application/grpc"}};
  EXPECT_FALSE(Common::isGrpcRequestHeaders(is_not));
}

TEST(GrpcContextTest, IsGrpcResponseHeader) {
  Http::TestResponseHeaderMapImpl grpc_status_only{{":status", "500"}, {"grpc-status", "14"}};
  EXPECT_TRUE(Common::isGrpcResponseHeaders(grpc_status_only, true));
  EXPECT_FALSE(Common::isGrpcResponseHeaders(grpc_status_only, false));

  Http::TestResponseHeaderMapImpl grpc_response_header{{":status", "200"},
                                                       {"content-type", "application/grpc"}};
  EXPECT_FALSE(Common::isGrpcResponseHeaders(grpc_response_header, true));
  EXPECT_TRUE(Common::isGrpcResponseHeaders(grpc_response_header, false));

  Http::TestResponseHeaderMapImpl json_response_header{{":status", "200"},
                                                       {"content-type", "application/json"}};
  EXPECT_FALSE(Common::isGrpcResponseHeaders(json_response_header, true));
  EXPECT_FALSE(Common::isGrpcResponseHeaders(json_response_header, false));
}

TEST(GrpcContextTest, IsProtobufRequestHeader) {
  Http::TestRequestHeaderMapImpl is{
      {":method", "GET"}, {":path", "/"}, {"content-type", "application/x-protobuf"}};
  EXPECT_TRUE(Common::isProtobufRequestHeaders(is));

  Http::TestRequestHeaderMapImpl is_not{{":method", "CONNECT"},
                                        {"content-type", "application/x-protobuf"}};
  EXPECT_FALSE(Common::isProtobufRequestHeaders(is_not));
}

// Ensure that the correct gPRC header is constructed for a Buffer::Instance.
TEST(GrpcContextTest, PrependGrpcFrameHeader) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add("test", 4);
  std::array<char, 5> expected_header;
  expected_header[0] = 0; // flags
  const uint32_t nsize = htonl(4);
  std::memcpy(&expected_header[1], reinterpret_cast<const void*>(&nsize), sizeof(uint32_t));
  std::string header_string(&expected_header[0], 5);
  Common::prependGrpcFrameHeader(*buffer);
  EXPECT_EQ(buffer->toString(), header_string + "test");
}

} // namespace Grpc
} // namespace Envoy
