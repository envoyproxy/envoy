#include "source/common/protobuf/protovalidate_util.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "google/protobuf/wrappers.pb.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace ProtobufMessage {
namespace {

/**
 * Test fixture for ProtovalidateUtil functionality.
 * Verifies protovalidate integration and error handling.
 */
class ProtovalidateUtilTest : public testing::Test {};

TEST_F(ProtovalidateUtilTest, IsAvailableReturnsCorrectValue) {
  // Verify availability matches compile-time feature flag.
#ifdef ENVOY_ENABLE_PROTOVALIDATE
  EXPECT_TRUE(ProtovalidateUtil::isAvailable());
#else
  EXPECT_FALSE(ProtovalidateUtil::isAvailable());
#endif
}

TEST_F(ProtovalidateUtilTest, ValidateSimpleMessage) {
  // Validation succeeds on well-formed message without constraints.
  google::protobuf::StringValue test_message;
  test_message.set_value("test");

  auto status = ProtovalidateUtil::validate(test_message);
  EXPECT_TRUE(status.ok()) << status.ToString();
}

TEST_F(ProtovalidateUtilTest, ValidateEmptyMessage) {
  // Empty messages pass validation when no constraints are defined.
  google::protobuf::StringValue test_message;

  auto status = ProtovalidateUtil::validate(test_message);
  EXPECT_TRUE(status.ok()) << status.ToString();
}

TEST_F(ProtovalidateUtilTest, FormatValidationError) {
  // Error formatting includes message type and details.
  google::protobuf::StringValue test_message;
  test_message.set_value("test");

  std::string error_details = "field validation failed";
  std::string formatted_error =
      ProtovalidateUtil::formatValidationError(test_message, error_details);

  EXPECT_THAT(formatted_error, testing::HasSubstr("google.protobuf.StringValue"));
  EXPECT_THAT(formatted_error, testing::HasSubstr("field validation failed"));
}

TEST_F(ProtovalidateUtilTest, MultipleValidationCalls) {
  // Multiple validation calls handle different messages correctly.
  google::protobuf::StringValue test_message1;
  test_message1.set_value("test1");

  google::protobuf::StringValue test_message2;
  test_message2.set_value("test2");

  EXPECT_TRUE(ProtovalidateUtil::validate(test_message1).ok());
  EXPECT_TRUE(ProtovalidateUtil::validate(test_message2).ok());
}

} // namespace
} // namespace ProtobufMessage
} // namespace Envoy
