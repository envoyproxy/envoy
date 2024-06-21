#include "source/extensions/filters/http/proto_message_logging/logging_util/logging_util.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/compat/status_macros.h"
#include "ocpdiag/core/testing/status_matchers.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "proto_field_extraction/test_utils/utils.h"
#include "proto_processing_lib/proto_scrubber/cloud_audit_log_field_checker.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"
#include "proto_processing_lib/proto_scrubber/utility.h"
#include "src/google/protobuf/util/converter/type_info.h"
#include "test/proto/logging.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {
namespace {

using ::google::protobuf::FieldMask;
using ::google::protobuf::Struct;
using ::google::protobuf::Type;
// using ::google::protobuf::contrib::parse_proto::ParseTextProtoOrDie;
using ::google::protobuf::field_extraction::CordMessageData;
using ::google::protobuf::field_extraction::testing::TypeHelper;
using ::logging::TestRequest;
using ::logging::TestResponse;

using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::Envoy::StatusHelpers::StatusIs;

using ::proto_processing_lib::proto_scrubber::CloudAuditLogFieldChecker;
using ::proto_processing_lib::proto_scrubber::ProtoScrubber;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using ::testing::ValuesIn;

const char kTestRequest[] = R"pb(
  id: 123445
  bucket {
    name: "test-bucket"
    ratio: 0.8
    objects: "test-object-1"
    objects: "test-object-2"
    objects: "test-object-3"
  }
  proto2_message {
    repeated_strings: [ "repeated-string-0", "repeated-string-1" ]
    repeated_enum: [ PROTO2_ALPHA, PROTO2_BETA, PROTO2_ALPHA, PROTO2_GAMMA ]
    repeated_double: [ 0.42 ]
    repeated_float: [ 0.42, 1.98 ]
    repeated_int64: [ 12, -5, 20 ]
    repeated_uint64: [ 12, 190, 20, 800 ]
    repeated_int32: [ 1, -5, 4, 100, -780 ]
    repeated_fixed64: [ 1, 2, 89, 3, 56, 49 ]
    repeated_fixed32: [ 1, 2, 89, 3, 56, 49, 90 ]
    repeated_bool: [ True, False, False, True, True ]
    repeated_uint32: [ 1, 2, 89, 3, 56, 49, 90, 0 ]
    repeated_sfixed64: [ 1, 2, -89, 3, 56, -49 ]
    repeated_sfixed32: [ 1, 2, -89, 3, 56, -49, 90 ]
    repeated_sint32: [ 12, -5, 0, -7, 180 ]
    repeated_sint64: [ 12, -5, 0, -9, 90, 39028 ]
  }
  repeated_strings: [ "repeated-string-0" ]
  repeated_enum: [ ALPHA, BETA, TEST_ENUM_UNSPECIFIED, GAMMA ]
  repeated_double: [ 0.42 ]
  repeated_float: [ 0.42, 1.98 ]
  repeated_int64: [ 12, -5, 20 ]
  repeated_uint64: [ 12, 190, 20, 800 ]
  repeated_int32: [ 1, -5, 4, 100, -780 ]
  repeated_fixed64: [ 1, 2, 89, 3, 56, 49 ]
  repeated_fixed32: [ 1, 2, 89, 3, 56, 49, 90 ]
  repeated_bool: [ True, False, False, True, True ]
  repeated_uint32: [ 1, 2, 89, 3, 56, 49, 90, 0 ]
  repeated_sfixed64: [ 1, 2, -89, 3, 56, -49 ]
  repeated_sfixed32: [ 1, 2, -89, 3, 56, -49, 90 ]
  repeated_sint32: [ 12, -5, 0, -7, 180 ]
  repeated_sint64: [ 12, -5, 0, -9, 90, 39028 ]
)pb";

const char kTestResponse[] = R"pb(
  buckets {
    name: "test-bucket-0"
    ratio: 0.8
    objects: "test-object-01"
    objects: "test-object-02"
    objects: "test-object-03"
  }
  buckets {
    name: "test-bucket-1"
    ratio: 0.9
    objects: "test-object-11"
    objects: "test-object-12"
    objects: "test-object-13"
  }
  sub_buckets {
    key: "test-bucket-2"
    value { name: "test-bucket-2" ratio: 0.5 objects: "test-object-21" }
  }
  sub_buckets {
    key: "test-bucket-3"
    value { name: "test-bucket-3" ratio: 0.2 objects: "test-object-31" }
  }
  bucket_present { name: "bucket_present" }
  sub_message { bucket_present { name: "bucket_present" } }
)pb";

class AuditLoggingUtilTest : public ::testing::Test {
 protected:
  AuditLoggingUtilTest() = default;
  const google::protobuf::Type* FindType(const std::string& type_url) {
    absl::StatusOr<const google::protobuf::Type*> result =
        type_helper_->ResolveTypeUrl(type_url);
    if (!result.ok()) {
      return nullptr;
    }
    return result.value();
  }

  void SetUp() override {
    const std::string descriptor_path = TestEnvironment::runfilesPath(
        "test/proto/logging.descriptor");
    absl::StatusOr<std::unique_ptr<TypeHelper>> status =
        TypeHelper::Create(descriptor_path);
    type_helper_ = std::move(status.value());

    type_finder_ = std::bind_front(&AuditLoggingUtilTest::FindType, this);

    if (!google::protobuf::TextFormat::ParseFromString(kTestRequest,
                                                       &test_request_proto_)) {
      // ADD_FAILURE() << "Failed to parse textproto: " << text_proto_;
    }

    // test_request_proto_ = ParseTextProtoOrDie(kTestRequest);
    test_request_raw_proto_ =
        CordMessageData(test_request_proto_.SerializeAsCord());
    request_type_ = type_finder_(
        "type.googleapis.com/"
        "logging.TestRequest");

    if (!google::protobuf::TextFormat::ParseFromString(kTestResponse,
                                                       &test_response_proto_)) {
      // ADD_FAILURE() << "Failed to parse textproto: " << text_proto_;
    }
    // test_response_proto_ = ParseTextProtoOrDie(kTestResponse);
    test_response_raw_proto_ =
        CordMessageData(test_response_proto_.SerializeAsCord());
    response_type_ = type_finder_(
        "type.googleapis.com/"
        "logging.TestResponse");
  }

  FieldMask* GetFieldMaskWith(const std::string& path) {
    field_mask_.clear_paths();
    field_mask_.add_paths(path);
    return &field_mask_;
  }

  // A TypeHelper for testing.
  std::unique_ptr<TypeHelper> type_helper_ = nullptr;

  std::function<const Type*(const std::string&)> type_finder_;

  TestRequest test_request_proto_;
  google::protobuf::field_extraction::CordMessageData test_request_raw_proto_;
  const Type* request_type_;

  TestResponse test_response_proto_;
  google::protobuf::field_extraction::CordMessageData test_response_raw_proto_;
  const Type* response_type_;

  FieldMask field_mask_;
};

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_bytes) {
  EXPECT_EQ(3, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("bucket.objects"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_string) {
  EXPECT_EQ(1, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_strings"),
                                        test_request_raw_proto_));
  EXPECT_EQ(2, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_strings"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_message) {
  EXPECT_EQ(2, ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                        GetFieldMaskWith("buckets"),
                                        test_response_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_map) {
  EXPECT_EQ(2, ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                        GetFieldMaskWith("sub_buckets"),
                                        test_response_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_enum) {
  EXPECT_EQ(4, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_enum"),
                                        test_request_raw_proto_));
  EXPECT_EQ(4, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_enum"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_double) {
  EXPECT_EQ(1, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_double"),
                                        test_request_raw_proto_));
  EXPECT_EQ(1, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_double"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_float) {
  EXPECT_EQ(2, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_float"),
                                        test_request_raw_proto_));
  EXPECT_EQ(2, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_float"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_int64) {
  EXPECT_EQ(3, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_int64"),
                                        test_request_raw_proto_));
  EXPECT_EQ(3, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_int64"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_uint64) {
  EXPECT_EQ(4, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_uint64"),
                                        test_request_raw_proto_));
  EXPECT_EQ(4, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_uint64"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_int32) {
  EXPECT_EQ(5, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_int32"),
                                        test_request_raw_proto_));
  EXPECT_EQ(5, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_int32"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_fixed64) {
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_fixed64"),
                                        test_request_raw_proto_));
  EXPECT_EQ(6, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_fixed64"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_fixed32) {
  EXPECT_EQ(7, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_fixed32"),
                                        test_request_raw_proto_));
  EXPECT_EQ(7, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_fixed32"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_bool) {
  EXPECT_EQ(5, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_bool"),
                                        test_request_raw_proto_));

  EXPECT_EQ(5, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_bool"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_uint32) {
  EXPECT_EQ(8, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_uint32"),
                                        test_request_raw_proto_));
  EXPECT_EQ(8, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_uint32"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sfixed64) {
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_sfixed64"),
                                        test_request_raw_proto_));
  EXPECT_EQ(6, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_sfixed64"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sfixed32) {
  EXPECT_EQ(7, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_sfixed32"),
                                        test_request_raw_proto_));
  EXPECT_EQ(7, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_sfixed32"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sint32) {
  EXPECT_EQ(5, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_sint32"),
                                        test_request_raw_proto_));
  EXPECT_EQ(5, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_sint32"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sint64) {
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_sint64"),
                                        test_request_raw_proto_));
  EXPECT_EQ(6, ExtractRepeatedFieldSize(
                   *request_type_, type_finder_,
                   GetFieldMaskWith("proto2_message.repeated_sint64"),
                   test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_DefaultValue) {
  test_request_proto_.clear_repeated_strings();
  test_request_raw_proto_ =
      CordMessageData(test_request_proto_.SerializeAsCord());
  EXPECT_EQ(0, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_strings"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_EmptyPath) {
  EXPECT_LT(
      ExtractRepeatedFieldSize(*request_type_, type_finder_,
                               GetFieldMaskWith(""), test_request_raw_proto_),
      0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_NonRepeatedField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("bucket.ratio"),
                                     test_request_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_UnknownField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("bucket.unknown"),
                                     test_request_raw_proto_),
            0);
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("unknown"),
                                     test_request_raw_proto_),
            0);
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("unknown1.unknown2"),
                                     test_request_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest,
       ExtractRepeatedFieldSize_Error_NonLeafPrimitiveTypeField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("bucket.name.unknown"),
                                     test_request_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_NonLeafMapField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                     GetFieldMaskWith("sub_buckets.objects"),
                                     test_response_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest,
       ExtractRepeatedFieldSize_Error_NonLeafRepeatedField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                     GetFieldMaskWith("buckets.name"),
                                     test_response_raw_proto_),
            0);
  EXPECT_LT(ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                     GetFieldMaskWith("buckets.objects"),
                                     test_response_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_NullptrFieldMask) {
  EXPECT_GT(0, ExtractRepeatedFieldSize(*request_type_, type_finder_, nullptr,
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_EmptyFieldMask) {
  FieldMask field_mask;
  EXPECT_GT(0, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        &field_mask_, test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_OK) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_,
                                      "bucket.name", test_request_raw_proto_),
              IsOkAndHolds("test-bucket"));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_OK_DefaultValue) {
  test_request_proto_.mutable_bucket()->clear_name();
  test_request_raw_proto_ =
      CordMessageData(test_request_proto_.SerializeAsCord());
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_,
                                      "bucket.name", test_request_raw_proto_),
              IsOkAndHolds(""));

  test_request_proto_.clear_bucket();
  test_request_raw_proto_ =
      CordMessageData(test_request_proto_.SerializeAsCord());
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_,
                                      "bucket.name", test_request_raw_proto_),
              IsOkAndHolds(""));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_EmptyPath) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_UnknownField) {
  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "bucket.unknown",
                              test_request_raw_proto_),
      StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "unknown",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "unknown1.unknown2",
                              test_request_raw_proto_),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest,
       ExtractStringFieldValue_Error_RepeatedStringLeafNode) {
  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "repeated_strings",
                              test_request_raw_proto_),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_NonStringLeafNode) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_,
                                      "bucket.ratio", test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(ExtractStringFieldValue(*response_type_, type_finder_,
                                      "sub_buckets", test_response_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_InvalidTypeFinder) {
  auto invalid_type_finder = [](absl::string_view type_url) { return nullptr; };

  EXPECT_THAT(ExtractStringFieldValue(*request_type_, invalid_type_finder,
                                      "bucket.ratio", test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(StructUtilTest, RedactPaths_Basic) {
  std::string proto_struct_string =
      "fields {"
      "  key: \"nested\""
      "  value {"
      "    struct_value {"
      "      fields {"
      "        key: \"deeper_nest\""
      "        value {"
      "          struct_value: {"
      "            fields {"
      "              key: \"nice_field\""
      "              value {"
      "                string_value: \"nice value\""
      "              }"
      "            }"
      "          }"
      "        }"
      "      }"
      "    }"
      "  }"
      "}";
  Struct proto_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto_struct_string,
                                                      &proto_struct));

  std::vector<std::string> paths_to_redact = {"nested.deeper_nest"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string =
      "fields {"
      "  key: \"nested\""
      "  value {"
      "    struct_value {"
      "      fields {"
      "        key: \"deeper_nest\""
      "        value {"
      "          struct_value: {}"
      "        }"
      "      }"
      "    }"
      "  }"
      "}";
  Struct expected_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(expected_struct_string,
                                                      &expected_struct));
  EXPECT_TRUE(::google::protobuf::util::MessageDifferencer::Equals(
      expected_struct, proto_struct));
}

TEST(StructUtilTest, RedactPaths_HandlesOneOfFields) {
  std::string proto_struct_string =
      "fields {"
      "  key: \"nested_value\""
      "  value {"
      "    struct_value {"
      "      fields {"
      "        key: \"uint32_field\""
      "        value {"
      "          number_value: 123"
      "        }"
      "      }"
      "    }"
      "  }"
      "}";
  Struct proto_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto_struct_string,
                                                      &proto_struct));

  std::vector<std::string> paths_to_redact = {"nested_value"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string =
      "fields {"
      "  key: \"nested_value\""
      "  value {"
      "    struct_value {}"
      "  }"
      "}";
  Struct expected_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(expected_struct_string,
                                                      &expected_struct));
  EXPECT_TRUE(::google::protobuf::util::MessageDifferencer::Equals(
      expected_struct, proto_struct));
}

TEST(StructUtilTest, RedactPaths_AllowsRepeatedLeafMessageType) {
  std::string proto_struct_string =
      "fields {"
      "  key: \"repeated_message_field\""
      "  value {"
      "    list_value {"
      "      values {"
      "        struct_value {"
      "          fields {"
      "            key: \"uint32_field\""
      "            value {"
      "              number_value: 123"
      "            }"
      "          }"
      "        }"
      "      }"
      "      values {"
      "        struct_value {"
      "          fields {"
      "            key: \"uint32_field\""
      "            value {"
      "              number_value: 456"
      "            }"
      "          }"
      "        }"
      "      }"
      "    }"
      "  }"
      "}";
  Struct proto_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto_struct_string,
                                                      &proto_struct));

  std::vector<std::string> paths_to_redact = {"repeated_message_field"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string =
      "fields {"
      "  key: \"repeated_message_field\""
      "  value {"
      "    list_value {"
      "      values {"
      "        struct_value {}"
      "      }"
      "      values {"
      "        struct_value {}"
      "      }"
      "    }"
      "  }"
      "}";
  Struct expected_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(expected_struct_string,
                                                      &expected_struct));
  EXPECT_TRUE(::google::protobuf::util::MessageDifferencer::Equals(
      expected_struct, proto_struct));
}

TEST(StructUtilTest, RedactPaths_AllowsRepeatedNonLeafMessageType) {
  std::string proto_struct_string =
      "fields {"
      "  key: \"repeated_message_field\""
      "  value {"
      "    list_value {"
      "      values {"
      "        struct_value {"
      "          fields {"
      "            key: \"uint32_field\""
      "            value {"
      "              number_value: 123"
      "            }"
      "          }"
      "          fields {"
      "            key: \"deeper_nest\""
      "            value {"
      "              struct_value {"
      "                fields {"
      "                  key: \"nice_field\""
      "                  value: {"
      "                    string_value: \"nice value\""
      "                  }"
      "                }"
      "              }"
      "            }"
      "          }"
      "        }"
      "      }"
      "      values {"
      "        struct_value {"
      "          fields {"
      "            key: \"uint32_field\""
      "            value {"
      "              number_value: 456"
      "            }"
      "          }"
      "        }"
      "      }"
      "    }"
      "  }"
      "}";
  Struct proto_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto_struct_string,
                                                      &proto_struct));

  std::vector<std::string> paths_to_redact = {
      "repeated_message_field.deeper_nest"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string =
      "fields {"
      "  key: \"repeated_message_field\""
      "  value {"
      "    list_value {"
      "      values {"
      "        struct_value {"
      "          fields {"
      "            key: \"uint32_field\""
      "            value {"
      "              number_value: 123"
      "            }"
      "          }"
      "          fields {"
      "            key: \"deeper_nest\""
      "            value {"
      "              struct_value {}"
      "            }"
      "          }"
      "        }"
      "      }"
      "      values {"
      "        struct_value {"
      "          fields {"
      "            key: \"uint32_field\""
      "            value {"
      "              number_value: 456"
      "            }"
      "          }"
      "        }"
      "      }"
      "    }"
      "  }"
      "}";
  Struct expected_struct;
  CHECK(google::protobuf::TextFormat::ParseFromString(expected_struct_string,
                                                      &expected_struct));
  EXPECT_TRUE(::google::protobuf::util::MessageDifferencer::Equals(
      expected_struct, proto_struct));
}

struct ResourceNameTestCase {
  std::string test_name;
  std::string resource_name;
  std::string expected_location;
};

class ExtractLocationIdFromResourceNameTest
    : public ::testing::TestWithParam<ResourceNameTestCase> {};

TEST_P(ExtractLocationIdFromResourceNameTest, Test) {
  // Empty resource name - should return empty string.
  const ResourceNameTestCase& params = GetParam();
  EXPECT_EQ(ExtractLocationIdFromResourceName(params.resource_name),
            params.expected_location);
}

INSTANTIATE_TEST_SUITE_P(
    ExtractLocationIdFromResourceNameTests,
    ExtractLocationIdFromResourceNameTest,
    ValuesIn<ResourceNameTestCase>({
        {"EmptyResource", "", ""},
        {"NoLocation", "projects/123/buckets/abc", ""},
        {"LocationSubstring", "my_locations/123/frw1/fw1", ""},
        {"MissingLocationValue", "projects/123/buckets/abc/locations/", ""},
        {"EmptyLocationValue", "projects/123/locations//buckets/abc", ""},
        {"LocationInMiddle", "projects/123/locations/456/buckets/abc", "456"},
        {"LocationAtEnd", "projects/123/locations/456", "456"},
        {"LocationAtStartQualified", "//locations/123/", "123"},
        {"LocationOnly", "locations/123", "123"},
        {"LocationAtStartWithMore", "locations/123/frwl/fw1", "123"},
        {"RegionSubstring", "my_regions/123/frw1/fw1", ""},
        {"MissingRegionValue", "projects/123/buckets/abc/regions/", ""},
        {"EmptyRegionValue", "projects/123/regions//buckets/abc", ""},
        {"RegionInMiddle", "projects/123/regions/456/buckets/abc", "456"},
        {"RegionAtEnd", "projects/123/regions/456", "456"},
        {"RegionAtStartQualified", "//regions/123/", "123"},
        {"RegionOnly", "regions/123", "123"},
        {"RegionAtStartWithMore", "regions/123/frwl/fw1", "123"},
    }),
    [](const ::testing::TestParamInfo<
        ExtractLocationIdFromResourceNameTest::ParamType>& info) {
      return info.param.test_name;
    });

}  // namespace
}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
