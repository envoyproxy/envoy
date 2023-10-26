#include "source/extensions/filters/http/connect_grpc_bridge/end_stream_response.h"

#include "test/test_common/global.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {
namespace {

using GrpcStatus = Grpc::Status::WellKnownGrpcStatus;

class EndStreamResponseTest : public testing::Test {
protected:
  void compareJson(const std::string& expected, const std::string& actual) {
    ProtobufWkt::Value expected_value, actual_value;
    TestUtility::loadFromJson(expected, expected_value);
    TestUtility::loadFromJson(actual, actual_value);
    EXPECT_TRUE(TestUtility::protoEqual(expected_value, actual_value));
  }
};

TEST_F(EndStreamResponseTest, StatusCodeToConnectUnaryStatus) {
  const std::vector<std::pair<Grpc::Status::GrpcStatus, uint64_t>> test_set = {
      {GrpcStatus::Canceled, 408},           {GrpcStatus::DeadlineExceeded, 408},
      {GrpcStatus::InvalidArgument, 400},    {GrpcStatus::OutOfRange, 400},
      {GrpcStatus::NotFound, 404},           {GrpcStatus::Unimplemented, 404},
      {GrpcStatus::AlreadyExists, 409},      {GrpcStatus::Aborted, 409},
      {GrpcStatus::PermissionDenied, 403},   {GrpcStatus::ResourceExhausted, 429},
      {GrpcStatus::FailedPrecondition, 412}, {GrpcStatus::Unavailable, 503},
      {GrpcStatus::Unauthenticated, 401},    {GrpcStatus::Unknown, 500},
      {GrpcStatus::Internal, 500},           {GrpcStatus::DataLoss, 500},
      {GrpcStatus::InvalidCode, 500},
  };
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, statusCodeToConnectUnaryStatus(test_case.first));
  }
}

TEST_F(EndStreamResponseTest, SerializeJsonError) {
  ProtobufWkt::Any detail;
  detail.set_type_url("type.url");
  detail.set_value("protobuf");
  const std::vector<std::pair<Error, std::string>> test_set = {
      {{GrpcStatus::Canceled, "", {}}, R"EOF({"code":"canceled"})EOF"},
      {{GrpcStatus::Unknown, "", {}}, R"EOF({"code":"unknown"})EOF"},
      {{GrpcStatus::InvalidArgument, "", {}}, R"EOF({"code":"invalid_argument"})EOF"},
      {{GrpcStatus::DeadlineExceeded, "", {}}, R"EOF({"code":"deadline_exceeded"})EOF"},
      {{GrpcStatus::NotFound, "", {}}, R"EOF({"code":"not_found"})EOF"},
      {{GrpcStatus::AlreadyExists, "", {}}, R"EOF({"code":"already_exists"})EOF"},
      {{GrpcStatus::PermissionDenied, "", {}}, R"EOF({"code":"permission_denied"})EOF"},
      {{GrpcStatus::ResourceExhausted, "", {}}, R"EOF({"code":"resource_exhausted"})EOF"},
      {{GrpcStatus::FailedPrecondition, "", {}}, R"EOF({"code":"failed_precondition"})EOF"},
      {{GrpcStatus::Aborted, "", {}}, R"EOF({"code":"aborted"})EOF"},
      {{GrpcStatus::OutOfRange, "", {}}, R"EOF({"code":"out_of_range"})EOF"},
      {{GrpcStatus::Unimplemented, "", {}}, R"EOF({"code":"unimplemented"})EOF"},
      {{GrpcStatus::Internal, "", {}}, R"EOF({"code":"internal"})EOF"},
      {{GrpcStatus::Unavailable, "", {}}, R"EOF({"code":"unavailable"})EOF"},
      {{GrpcStatus::DataLoss, "", {}}, R"EOF({"code":"data_loss"})EOF"},
      {{GrpcStatus::Unauthenticated, "", {}}, R"EOF({"code":"unauthenticated"})EOF"},
      {{9001, "", {}}, R"EOF({"code":"code_9001"})EOF"},
      {{GrpcStatus::Aborted, "Request aborted", {}},
       R"EOF({"code":"aborted","message":"Request aborted"})EOF"},
      {{GrpcStatus::Aborted, "", {detail}},
       R"EOF({"code":"aborted","details":[{"type":"type.url","value":"cHJvdG9idWY="}]})EOF"},
  };
  for (const auto& test_case : test_set) {
    std::string result;
    EXPECT_TRUE(serializeJson(test_case.first, result));
    compareJson(test_case.second, result);
  }
}

TEST_F(EndStreamResponseTest, SerializeJsonEndOfStream) {
  const std::vector<std::pair<EndStreamResponse, std::string>> test_set = {
      {{{}, {}}, "{}"},
      {{{{GrpcStatus::Canceled, "", {}}}, {}}, R"EOF({"error":{"code":"canceled"}})EOF"},
      {{{}, {{"test", {"1", "2"}}}}, R"EOF({"metadata":{"test":["1","2"]}})EOF"},
  };
  for (const auto& test_case : test_set) {
    std::string result;
    EXPECT_TRUE(serializeJson(test_case.first, result));
    compareJson(test_case.second, result);
  }
}

} // namespace
} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
