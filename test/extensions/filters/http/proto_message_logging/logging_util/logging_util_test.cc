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
// #include "test/test_common/status_utility.h"
// #include "protocolbuffers/protobuf/upb/test/parse_text_proto.h"
// #include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {
namespace {

using ::google::protobuf::FieldMask;
using google::protobuf::Struct;
using ::google::protobuf::Type;
using ::google::protobuf::contrib::parse_proto::ParseTextProtoOrDie;
using ::google::protobuf::field_extraction::CordMessageData;
using ::google::protobuf::field_extraction::testing::TypeHelper;
using ::logging::TestRequest;
using ::logging::TestResponse;
using ::ocpdiag::testing::StatusIs;
using ::proto_processing_lib::proto_scrubber::CloudAuditLogFieldChecker;
using ::proto_processing_lib::proto_scrubber::ProtoScrubber;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using ::testing::ValuesIn;
// using ::Envoy::StatusHelpers::IsOkAndHolds;
// using ::util::error::INVALID_ARGUMENT;

// Check that a StatusOr is OK and has a value equal to or matching its
// argument.
//
// For example:
//
// StatusOr<int> status(3);
// EXPECT_THAT(status, IsOkAndHolds(3));
// EXPECT_THAT(status, IsOkAndHolds(Gt(2)));
MATCHER_P(IsOkAndHolds, expected, "") {
  if (!arg.ok()) {
    // *result_listener << "which has unexpected status: " << arg.status();
    return false;
  }
  if (!::testing::Matches(expected)(*arg)) {
    // *result_listener << "which has wrong value: "
    //                  << ::testing::PrintToString(*arg);
    return false;
  }
  return true;
}

const char kTestRequest[] = R"pb(
  id: 123445
  bucket {
    name: "test-bucket"
    ratio: 0.8
    objects: "test-object-1"
    objects: "test-object-2"
    objects: "test-object-3"
  }
  monitored_resource_string: "library.googleapis.com/region/us-east1-b"
  monitored_resource {
    type: "library"
    labels { key: "library.googleapis.com/region" value: "us-east1-b" }
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
  monitored_resource {
    type: "library"
    labels { key: "library.googleapis.com/region" value: "us-east1-b" }
    labels { key: "library.googleapis.com/resource_id" value: "id00" }
    labels { key: "library.googleapis.com/resource_type" value: "object" }
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
    const std::string descriptor_path =
        "audit_logging_util_test_proto_descriptor.pb";
    absl::StatusOr<std::unique_ptr<TypeHelper>> status =
        TypeHelper::Create(descriptor_path);
    type_helper_ = std::move(status.value());

    typeinfo_ = type_helper_->Info();

    type_finder_ = std::bind_front(&AuditLoggingUtilTest::FindType, this);

    test_request_proto_ = ParseTextProtoOrDie(kTestRequest);
    test_request_raw_proto_ =
        CordMessageData(test_request_proto_.SerializeAsCord());
    request_type_ = type_finder_(
        "type.googleapis.com/"
        "logging.TestRequest");

    test_response_proto_ = ParseTextProtoOrDie(kTestResponse);
    test_response_raw_proto_ =
        CordMessageData(test_response_proto_.SerializeAsCord());
    response_type_ = type_finder_(
        "type.googleapis.com/"
        "logging.TestResponse");
  }

  void TestScrubToStruct(const std::vector<std::string>& paths,
                         const google::protobuf::Type* type,
                         const FieldMask* redact_field_mask,
                         const google::protobuf::Message& proto,
                         const google::protobuf::Message& expected_proto) {
    CloudAuditLogFieldChecker field_checker(type, type_finder_);
    ASSERT_OK(field_checker.AddOrIntersectFieldPaths(paths));
    ProtoScrubber proto_scrubber(type, type_finder_, {&field_checker},
                                 ScrubberContext::kTestScrubbing);

    CordMessageData raw_proto = CordMessageData(proto.SerializeAsCord());
    google::protobuf::Struct actual_struct, expected_struct;
    EXPECT_TRUE(ScrubToStruct(&proto_scrubber, *type, *typeinfo_, type_finder_,
                              redact_field_mask, &raw_proto, &actual_struct));

    raw_proto = CordMessageData(expected_proto.SerializeAsCord());
    ASSERT_OK(ConvertToStruct(raw_proto, *type, *typeinfo_, &expected_struct));

    // EXPECT_THAT(actual_struct,
    //             IgnoringRepeatedFieldOrdering(
    //                 ::google::protobuf::util::MessageDifferencer::Equals(
    //                     expected_struct)));
  }

  FieldMask* GetFieldMaskWith(const std::string& path) {
    field_mask_.clear_paths();
    field_mask_.add_paths(path);
    return &field_mask_;
  }

  const google::protobuf::util::converter::TypeInfo* typeinfo_;

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

  EXPECT_EQ(
      3, ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                  GetFieldMaskWith("monitored_resource.labels"),
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

  test_response_proto_.mutable_monitored_resource()->clear_labels();
  test_response_raw_proto_ =
      CordMessageData(test_response_proto_.SerializeAsCord());
  EXPECT_EQ(
      0, ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                  GetFieldMaskWith("monitored_resource.labels"),
                                  test_response_raw_proto_));
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

  EXPECT_LT(ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                     GetFieldMaskWith("monitored_resource"),
                                     test_response_raw_proto_),
            0);

  EXPECT_LT(
      ExtractRepeatedFieldSize(*response_type_, type_finder_,
                               GetFieldMaskWith("monitored_resource.type"),
                               test_response_raw_proto_),
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

  EXPECT_THAT(ExtractStringFieldValue(*response_type_, type_finder_,
                                      "monitored_resource.type",
                                      test_response_raw_proto_),
              IsOkAndHolds("library"));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_HasDuplicate) {
  std::string last_string = "boom!";
  CordMessageData extra_string;
  TestRequest append_request;
  append_request.set_monitored_resource_string(last_string);
  extra_string = CordMessageData(append_request.SerializeAsCord());

  test_request_raw_proto_.Append(extra_string.Cord());

  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_,
                                      "monitored_resource_string",
                                      test_request_raw_proto_),
              IsOkAndHolds(last_string));

  std::string another_string = "damnit!";
  CordMessageData another_extra_string;
  TestRequest another_append_request;
  another_append_request.set_monitored_resource_string(another_string);
  another_extra_string =
      CordMessageData(another_append_request.SerializeAsCord());

  test_request_raw_proto_.Append(another_extra_string.Cord());
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_,
                                      "monitored_resource_string",
                                      test_request_raw_proto_),
              IsOkAndHolds(another_string));
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
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Field mask path cannot be empty."));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_UnknownField) {
  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "bucket.unknown",
                              test_request_raw_proto_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Cannot find field 'unknown' in "
               "'logging.TestBucket' message."));

  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "unknown",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Cannot find field 'unknown' in "
                       "'logging."
                       "TestRequest' message."));

  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "unknown1.unknown2",
                              test_request_raw_proto_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Cannot find field 'unknown1' in "
               "'logging.TestRequest' "
               "message."));
}

TEST_F(AuditLoggingUtilTest,
       ExtractStringFieldValue_Error_RepeatedStringLeafNode) {
  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "repeated_strings",
                              test_request_raw_proto_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Field 'repeated_strings' is a repeated string field, only "
               "singular string field is accepted."));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_NonStringLeafNode) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_,
                                      "bucket.ratio", test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Field 'ratio' is not a singular string field."));

  EXPECT_THAT(ExtractStringFieldValue(*response_type_, type_finder_,
                                      "sub_buckets", test_response_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Field 'sub_buckets' is not a singular string field."));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_InvalidTypeFinder) {
  auto invalid_type_finder = [](absl::string_view type_url) { return nullptr; };

  EXPECT_THAT(ExtractStringFieldValue(*request_type_, invalid_type_finder,
                                      "bucket.ratio", test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Cannot find the type of field 'bucket'."));
}

TEST_F(AuditLoggingUtilTest, ConvertToStruct_Request) {
  google::protobuf::Struct my_struct, their_struct;

  EXPECT_OK(ConvertToStruct(test_request_raw_proto_, *request_type_, *typeinfo_,
                            &my_struct));
  // their_struct = cloud_logging::struct_util::ToStruct(test_request_proto_);

  // EXPECT_THAT(
  //     my_struct,
  //     Approximately(IgnoringRepeatedFieldOrdering(::google::protobuf::util::MessageDifferencer::Equals(their_struct)),
  //                   0.0000001));
}

TEST_F(AuditLoggingUtilTest, ConvertToStruct_Response) {
  google::protobuf::Struct my_struct, their_struct;

  EXPECT_OK(ConvertToStruct(test_response_raw_proto_, *response_type_,
                            *typeinfo_, &my_struct));
  // their_struct = cloud_logging::struct_util::ToStruct(test_response_proto_);

  // EXPECT_THAT(
  //     my_struct,
  //     Approximately(IgnoringRepeatedFieldOrdering(::google::protobuf::util::MessageDifferencer::Equals(their_struct)),
  //                   0.0000001));
}

TEST_F(AuditLoggingUtilTest, ScrubToStruct_Request) {
  TestRequest expected_proto = ParseTextProtoOrDie(R"pb(
    id: 123445
    bucket {
      ratio: 0.8
      objects: "test-object-1"
      objects: "test-object-2"
      objects: "test-object-3"
    }
  )pb");

  TestScrubToStruct({"bucket.ratio", "bucket.objects", "id"}, request_type_,
                    nullptr, test_request_proto_, expected_proto);
}

TEST_F(AuditLoggingUtilTest, ScrubToStruct_Response) {
  TestResponse expected_proto = ParseTextProtoOrDie(R"pb(
    buckets {
      objects: "test-object-01"
      objects: "test-object-02"
      objects: "test-object-03"
    }
    buckets {
      objects: "test-object-11"
      objects: "test-object-12"
      objects: "test-object-13"
    }
    monitored_resource {
      labels { key: "library.googleapis.com/region" value: "us-east1-b" }
      labels { key: "library.googleapis.com/resource_id" value: "id00" }
      labels { key: "library.googleapis.com/resource_type" value: "object" }
    }
    sub_buckets {
      key: "test-bucket-2"
      value { ratio: 0.5 objects: "test-object-21" }
    }
    sub_buckets {
      key: "test-bucket-3"
      value { ratio: 0.2 objects: "test-object-31" }
    }
    bucket_present {}
    sub_message { bucket_present {} }
  )pb");

  FieldMask redact_field_mask;
  redact_field_mask.add_paths("bucket_present");
  redact_field_mask.add_paths("bucket_absent");
  redact_field_mask.add_paths("sub_message.bucket_present");
  redact_field_mask.add_paths("sub_message.bucket_absent");
  TestScrubToStruct({"buckets.objects", "monitored_resource.labels",
                     "sub_buckets.ratio", "sub_buckets.objects"},
                    response_type_, &redact_field_mask, test_response_proto_,
                    expected_proto);
}

TEST_F(AuditLoggingUtilTest, ScrubToStruct_NullScrubber) {
  google::protobuf::Struct actual_struct;
  EXPECT_FALSE(ScrubToStruct(/* scrubber= */ nullptr, *request_type_,
                             *typeinfo_, type_finder_,
                             /* redact_message_field_mask= */ nullptr,
                             &test_request_raw_proto_, &actual_struct));
}

TEST_F(AuditLoggingUtilTest, ScrubToStruct_ScrubError) {
  google::protobuf::Struct actual_struct;

  CloudAuditLogFieldChecker field_checker(response_type_, type_finder_);
  ASSERT_OK(field_checker.AddOrIntersectFieldPaths(
      {"buckets.objects", "monitored_resource.labels"}));
  ProtoScrubber proto_scrubber(
      response_type_, [](absl::string_view type_name) { return nullptr; },
      {&field_checker}, ScrubberContext::kTestScrubbing);

  EXPECT_FALSE(ScrubToStruct(&proto_scrubber, *response_type_, *typeinfo_,
                             type_finder_,
                             /* redact_message_field_mask= */ nullptr,
                             &test_response_raw_proto_, &actual_struct));
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
