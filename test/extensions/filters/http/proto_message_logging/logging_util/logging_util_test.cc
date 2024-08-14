#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/proto_message_logging/logging_util/logging_util.h"

#include "test/proto/logging.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
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

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {
namespace {

using ::Envoy::Protobuf::Field;
using ::Envoy::Protobuf::FieldMask;
using ::Envoy::Protobuf::Type;
using ::Envoy::Protobuf::field_extraction::CordMessageData;
using ::Envoy::Protobuf::field_extraction::testing::TypeHelper;
using ::Envoy::Protobuf::io::CodedInputStream;
using ::Envoy::ProtobufWkt::Struct;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::Envoy::StatusHelpers::StatusIs;
using ::logging::TestRequest;
using ::logging::TestResponse;
using ::proto_processing_lib::proto_scrubber::CloudAuditLogFieldChecker;
using ::proto_processing_lib::proto_scrubber::ProtoScrubber;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using ::testing::ValuesIn;

// The type property value that will be included into the converted Struct.
constexpr char kTypeProperty[] = "@type";

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
  const Protobuf::Type* FindType(const std::string& type_url) {
    absl::StatusOr<const Protobuf::Type*> result = type_helper_->ResolveTypeUrl(type_url);
    if (!result.ok()) {
      return nullptr;
    }
    return result.value();
  }

  void SetUp() override {
    const std::string descriptor_path =
        TestEnvironment::runfilesPath("test/proto/logging.descriptor");
    absl::StatusOr<std::unique_ptr<TypeHelper>> status = TypeHelper::Create(descriptor_path);
    type_helper_ = std::move(status.value());

    type_finder_ = std::bind_front(&AuditLoggingUtilTest::FindType, this);

    if (!Protobuf::TextFormat::ParseFromString(kTestRequest, &test_request_proto_)) {
      LOG(ERROR) << "Failed to parse textproto: " << kTestRequest;
    }
    test_request_raw_proto_ = CordMessageData(test_request_proto_.SerializeAsCord());
    request_type_ = type_finder_("type.googleapis.com/"
                                 "logging.TestRequest");

    if (!Protobuf::TextFormat::ParseFromString(kTestResponse, &test_response_proto_)) {
      LOG(ERROR) << "Failed to parse textproto: " << kTestResponse;
    }
    test_response_raw_proto_ = CordMessageData(test_response_proto_.SerializeAsCord());
    response_type_ = type_finder_("type.googleapis.com/"
                                  "logging.TestResponse");

    labels_.clear();
  }

  FieldMask* GetFieldMaskWith(const std::string& path) {
    field_mask_.clear_paths();
    field_mask_.add_paths(path);
    return &field_mask_;
  }

  // Helper function to create a Struct with nested fields
  Struct CreateNestedStruct(const std::vector<std::string>& path, const std::string& final_value) {
    Struct root;
    Struct* current = &root;
    for (const auto& piece : path) {
      (*current->mutable_fields())[piece].mutable_struct_value();
      current = (*current->mutable_fields())[piece].mutable_struct_value();
    }
    (*current->mutable_fields())[final_value].mutable_string_value();
    return root;
  }

  // A TypeHelper for testing.
  std::unique_ptr<TypeHelper> type_helper_ = nullptr;

  std::function<const Type*(const std::string&)> type_finder_;

  TestRequest test_request_proto_;
  Protobuf::field_extraction::CordMessageData test_request_raw_proto_;
  const Type* request_type_;

  TestResponse test_response_proto_;
  Protobuf::field_extraction::CordMessageData test_response_raw_proto_;
  const Type* response_type_;

  FieldMask field_mask_;

  Envoy::Protobuf::Map<std::string, std::string> labels_;
};

TEST_F(AuditLoggingUtilTest, IsEmptyStruct_EmptyStruct) {
  ProtobufWkt::Struct message_struct;
  message_struct.mutable_fields()->insert({kTypeProperty, ProtobufWkt::Value()});
  EXPECT_TRUE(IsEmptyStruct(message_struct));
}

TEST_F(AuditLoggingUtilTest, IsEmptyStruct_NonEmptyStruct) {
  ProtobufWkt::Struct message_struct;
  message_struct.mutable_fields()->insert({kTypeProperty, ProtobufWkt::Value()});
  message_struct.mutable_fields()->insert({"another_field", ProtobufWkt::Value()});
  EXPECT_FALSE(IsEmptyStruct(message_struct));
}

TEST_F(AuditLoggingUtilTest, IsLabelName_ValidLabel) { EXPECT_TRUE(IsLabelName("{label}")); }

TEST_F(AuditLoggingUtilTest, IsLabelName_EmptyString) { EXPECT_FALSE(IsLabelName("")); }

TEST_F(AuditLoggingUtilTest, GetLabelName_RemovesCurlyBraces) {
  EXPECT_EQ(GetLabelName("{test}"), "test");
}

TEST_F(AuditLoggingUtilTest, GetLabelName_NoCurlyBraces) {
  EXPECT_EQ(GetLabelName("test"), "test");
}

TEST_F(AuditLoggingUtilTest, GetLabelName_EmptyString) { EXPECT_EQ(GetLabelName(""), ""); }

TEST_F(AuditLoggingUtilTest, GetMonitoredResourceLabels_BasicExtraction) {
  GetMonitoredResourceLabels("project/*/bucket/{bucket}/object/{object}",
                             "project/myproject/bucket/mybucket/object/myobject", &labels_);

  EXPECT_EQ(labels_.size(), 2);
  EXPECT_EQ(labels_["bucket"], "mybucket");
  EXPECT_EQ(labels_["object"], "myobject");
}

TEST_F(AuditLoggingUtilTest, StringToDirectiveMap_CorrectMapping) {
  const auto& map = StringToDirectiveMap();

  EXPECT_EQ(map.size(), 2);
  EXPECT_EQ(map.at(kAuditRedact), AuditDirective::AUDIT_REDACT);
  EXPECT_EQ(map.at(kAudit), AuditDirective::AUDIT);
}

TEST_F(AuditLoggingUtilTest, AuditDirectiveFromString_ValidDirective) {
  auto directive = AuditDirectiveFromString(kAuditRedact);
  ASSERT_TRUE(directive.has_value());
  EXPECT_EQ(directive.value(), AuditDirective::AUDIT_REDACT);

  directive = AuditDirectiveFromString(kAudit);
  ASSERT_TRUE(directive.has_value());
  EXPECT_EQ(directive.value(), AuditDirective::AUDIT);
}

TEST_F(AuditLoggingUtilTest, AuditDirectiveFromString_InvalidDirective) {
  auto directive = AuditDirectiveFromString("invalid_directive");
  EXPECT_FALSE(directive.has_value());
}

TEST_F(AuditLoggingUtilTest, GetMonitoredResourceLabels_MissingLabelsInResource) {
  GetMonitoredResourceLabels("project/*/bucket/{bucket}/object/{object}",
                             "project/myproject/bucket/mybucket", &labels_);

  EXPECT_EQ(labels_.size(), 1);
  EXPECT_EQ(labels_["bucket"], "mybucket");
  EXPECT_EQ(labels_.find("object"), labels_.end());
}

TEST_F(AuditLoggingUtilTest, GetMonitoredResourceLabels_ExtraSegmentsInResource) {
  GetMonitoredResourceLabels("project/*/bucket/{bucket}/object/{object}",
                             "project/myproject/bucket/mybucket/object/myobject/extra", &labels_);

  EXPECT_EQ(labels_.size(), 2);
  EXPECT_EQ(labels_["bucket"], "mybucket");
  EXPECT_EQ(labels_["object"], "myobject");
}

TEST_F(AuditLoggingUtilTest, GetMonitoredResourceLabels_WithNoLabels) {
  GetMonitoredResourceLabels("project/*/bucket/*/object/*",
                             "project/myproject/bucket/mybucket/object/myobject", &labels_);

  EXPECT_EQ(labels_.size(), 0);
}

TEST_F(AuditLoggingUtilTest, GetMonitoredResourceLabels_EmptyLabelExtractor) {
  GetMonitoredResourceLabels("", "project/myproject/bucket/mybucket/object/myobject", &labels_);

  EXPECT_EQ(labels_.size(), 0);
}

TEST_F(AuditLoggingUtilTest, RedactStructRecursively_EmptyPath) {
  Struct message_struct = CreateNestedStruct({"level1", "level2"}, "value");
  EXPECT_TRUE(RedactStructRecursively({}, {}, &message_struct).ok());
}

TEST_F(AuditLoggingUtilTest, RedactStructRecursively_InvalidPath) {
  Struct message_struct = CreateNestedStruct({"level1", "level2"}, "value");
  std::vector<std::string> path_pieces = {"invalid", "path_end"};
  EXPECT_TRUE(
      RedactStructRecursively(path_pieces.cbegin(), path_pieces.cend(), &message_struct).ok());

  // Verify that the field "level2" has been replaced with an empty Struct
  const auto& level1_field = message_struct.fields().at("level1");
  EXPECT_TRUE(level1_field.has_struct_value());
  const auto& level2_field = level1_field.struct_value().fields().at("level2");
  EXPECT_TRUE(level2_field.has_struct_value());
}

TEST_F(AuditLoggingUtilTest, RedactStructRecursively_ValidPath) {
  Struct message_struct = CreateNestedStruct({"level1", "level2"}, "value");
  std::vector<std::string> path_pieces = {"level1", "level2"};
  EXPECT_TRUE(
      RedactStructRecursively(path_pieces.cbegin(), path_pieces.cend(), &message_struct).ok());

  // Verify that the field "level2" has been replaced with an empty Struct
  const auto& level1_field = message_struct.fields().at("level1");
  EXPECT_TRUE(level1_field.has_struct_value());
  const auto& level2_field = level1_field.struct_value().fields().at("level2");
  EXPECT_TRUE(level2_field.has_struct_value());
}

TEST_F(AuditLoggingUtilTest, RedactStructRecursively_MissingIntermediateField) {
  Struct message_struct = CreateNestedStruct({"level1"}, "value");
  std::vector<std::string> path_pieces = {"level1", "level2"};
  EXPECT_TRUE(
      RedactStructRecursively(path_pieces.cbegin(), path_pieces.cend(), &message_struct).ok());

  // Verify that "level2" field is created as an empty Struct
  const auto& level1_field = message_struct.fields().at("level1");
  EXPECT_TRUE(level1_field.has_struct_value());
  const auto& level2_field = level1_field.struct_value().fields().at("level2");
  EXPECT_TRUE(level2_field.has_struct_value());
}

TEST_F(AuditLoggingUtilTest, RedactStructRecursively_EmptyPathPiece) {
  Struct message_struct = CreateNestedStruct({"level1", "level2"}, "value");
  std::vector<std::string> path_pieces = {"level1", ""};
  EXPECT_EQ(RedactStructRecursively(path_pieces.cbegin(), path_pieces.cend(), &message_struct),
            absl::InvalidArgumentError("path piece cannot be empty."));
}

TEST_F(AuditLoggingUtilTest, RedactStructRecursively_NullptrMessageStruct) {
  std::vector<std::string> path_pieces = {"level1"};
  EXPECT_EQ(RedactStructRecursively(path_pieces.cbegin(), path_pieces.cend(), nullptr),
            absl::InvalidArgumentError("message_struct cannot be nullptr."));
}

TEST_F(AuditLoggingUtilTest, IsMessageFieldPathPresent_EmptyPath) {
  EXPECT_EQ(
      IsMessageFieldPathPresent(*request_type_, type_finder_, "", test_request_raw_proto_).status(),
      absl::InvalidArgumentError("Field path cannot be empty."));
}

TEST_F(AuditLoggingUtilTest, IsMessageFieldPathPresent_EmptyType) {
  Type empty_type;
  EXPECT_EQ(
      IsMessageFieldPathPresent(empty_type, type_finder_, "id", test_request_raw_proto_).status(),
      absl::InvalidArgumentError("Cannot find field 'id' in '' message."));
}

TEST_F(AuditLoggingUtilTest, IsMessageFieldPathPresent_InvalidPath) {
  EXPECT_EQ(IsMessageFieldPathPresent(*request_type_, type_finder_, "invalid_path",
                                      test_request_raw_proto_)
                .status(),
            absl::InvalidArgumentError(
                "Cannot find field 'invalid_path' in 'logging.TestRequest' message."));
}

TEST_F(AuditLoggingUtilTest, IsMessageFieldPathPresent_ValidPath) {
  EXPECT_TRUE(
      IsMessageFieldPathPresent(*request_type_, type_finder_, "bucket", test_request_raw_proto_)
          .status()
          .ok());
}

TEST_F(AuditLoggingUtilTest, IsMessageFieldPathPresent_NotMessageField) {
  EXPECT_EQ(IsMessageFieldPathPresent(*request_type_, type_finder_, "repeated_strings",
                                      test_request_raw_proto_)
                .status(),
            absl::InvalidArgumentError("Field 'repeated_strings' is not a message type field."));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_bytes) {
  EXPECT_EQ(3,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("bucket.objects"), test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, FindSingularLastValue_SingleStringField) {
  Field field;
  field.set_name("id");
  field.set_number(1);
  field.set_kind(Field::TYPE_INT64);

  std::string data = "123445";
  auto result = FindSingularLastValue(
      &field, &test_request_raw_proto_.CreateCodedInputStreamWrapper()->Get());
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "");
}

TEST_F(AuditLoggingUtilTest, FindSingularLastValue_RepeatedStringField) {
  Field field;
  field.set_name("repeated_strings");
  field.set_number(3);
  field.set_kind(Field::TYPE_STRING);
  field.set_cardinality(Field::CARDINALITY_REPEATED);

  std::string data = "repeated-string-0";
  auto result = FindSingularLastValue(
      &field, &test_request_raw_proto_.CreateCodedInputStreamWrapper()->Get());
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "repeated-string-0");
}

TEST_F(AuditLoggingUtilTest, FindSingularLastValue_RepeatedMessageField) {
  Field field;
  field.set_name("bucket");
  field.set_number(2);
  field.set_kind(Field::TYPE_STRING);

  std::string data = "test-bucket";
  auto result = FindSingularLastValue(
      &field, &test_request_raw_proto_.CreateCodedInputStreamWrapper()->Get());
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(
      result.value(),
      "\n\vtest-bucket\x15\xCD\xCCL?\x1A\rtest-object-1\x1A\rtest-object-2\x1A\rtest-object-3");
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_string) {
  EXPECT_EQ(1, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_strings"),
                                        test_request_raw_proto_));
  EXPECT_EQ(2, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_strings"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_message) {
  EXPECT_EQ(2, ExtractRepeatedFieldSize(*response_type_, type_finder_, GetFieldMaskWith("buckets"),
                                        test_response_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_map) {
  EXPECT_EQ(2, ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                        GetFieldMaskWith("sub_buckets"), test_response_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_enum) {
  EXPECT_EQ(4,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_enum"), test_request_raw_proto_));
  EXPECT_EQ(4, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_enum"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_double) {
  EXPECT_EQ(1,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_double"), test_request_raw_proto_));
  EXPECT_EQ(1, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_double"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_float) {
  EXPECT_EQ(2,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_float"), test_request_raw_proto_));
  EXPECT_EQ(2, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_float"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_int64) {
  EXPECT_EQ(3,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_int64"), test_request_raw_proto_));
  EXPECT_EQ(3, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_int64"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_uint64) {
  EXPECT_EQ(4,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_uint64"), test_request_raw_proto_));
  EXPECT_EQ(4, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_uint64"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_int32) {
  EXPECT_EQ(5,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_int32"), test_request_raw_proto_));
  EXPECT_EQ(5, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_int32"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_fixed64) {
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_fixed64"),
                                        test_request_raw_proto_));
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_fixed64"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_fixed32) {
  EXPECT_EQ(7, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_fixed32"),
                                        test_request_raw_proto_));
  EXPECT_EQ(7, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_fixed32"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_bool) {
  EXPECT_EQ(5,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_bool"), test_request_raw_proto_));

  EXPECT_EQ(5, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_bool"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_uint32) {
  EXPECT_EQ(8,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_uint32"), test_request_raw_proto_));
  EXPECT_EQ(8, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_uint32"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sfixed64) {
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_sfixed64"),
                                        test_request_raw_proto_));
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_sfixed64"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sfixed32) {
  EXPECT_EQ(7, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_sfixed32"),
                                        test_request_raw_proto_));
  EXPECT_EQ(7, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_sfixed32"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sint32) {
  EXPECT_EQ(5,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_sint32"), test_request_raw_proto_));
  EXPECT_EQ(5, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_sint32"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_sint64) {
  EXPECT_EQ(6,
            ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("repeated_sint64"), test_request_raw_proto_));
  EXPECT_EQ(6, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("proto2_message.repeated_sint64"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_OK_DefaultValue) {
  test_request_proto_.clear_repeated_strings();
  test_request_raw_proto_ = CordMessageData(test_request_proto_.SerializeAsCord());
  EXPECT_EQ(0, ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                        GetFieldMaskWith("repeated_strings"),
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_EmptyPath) {
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_, GetFieldMaskWith(""),
                                     test_request_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_NonRepeatedField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_, GetFieldMaskWith("bucket.ratio"),
                                     test_request_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_UnknownField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("bucket.unknown"), test_request_raw_proto_),
            0);
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_, GetFieldMaskWith("unknown"),
                                     test_request_raw_proto_),
            0);
  EXPECT_LT(ExtractRepeatedFieldSize(*request_type_, type_finder_,
                                     GetFieldMaskWith("unknown1.unknown2"),
                                     test_request_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_NonLeafPrimitiveTypeField) {
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

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_NonLeafRepeatedField) {
  EXPECT_LT(ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                     GetFieldMaskWith("buckets.name"), test_response_raw_proto_),
            0);
  EXPECT_LT(ExtractRepeatedFieldSize(*response_type_, type_finder_,
                                     GetFieldMaskWith("buckets.objects"), test_response_raw_proto_),
            0);
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_Error_NullptrFieldMask) {
  EXPECT_GT(
      0, ExtractRepeatedFieldSize(*request_type_, type_finder_, nullptr, test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractRepeatedFieldSize_EmptyFieldMask) {
  FieldMask field_mask;
  EXPECT_GT(0, ExtractRepeatedFieldSize(*request_type_, type_finder_, &field_mask_,
                                        test_request_raw_proto_));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_OK) {
  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "bucket.name", test_request_raw_proto_),
      IsOkAndHolds("test-bucket"));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_OK_DefaultValue) {
  test_request_proto_.mutable_bucket()->clear_name();
  test_request_raw_proto_ = CordMessageData(test_request_proto_.SerializeAsCord());
  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "bucket.name", test_request_raw_proto_),
      IsOkAndHolds(""));

  test_request_proto_.clear_bucket();
  test_request_raw_proto_ = CordMessageData(test_request_proto_.SerializeAsCord());
  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "bucket.name", test_request_raw_proto_),
      IsOkAndHolds(""));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_EmptyPath) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "", test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_UnknownField) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "bucket.unknown",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(
      ExtractStringFieldValue(*request_type_, type_finder_, "unknown", test_request_raw_proto_),
      StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "unknown1.unknown2",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_RepeatedStringLeafNode) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "repeated_strings",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_NonStringLeafNode) {
  EXPECT_THAT(ExtractStringFieldValue(*request_type_, type_finder_, "bucket.ratio",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(ExtractStringFieldValue(*response_type_, type_finder_, "sub_buckets",
                                      test_response_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AuditLoggingUtilTest, ExtractStringFieldValue_Error_InvalidTypeFinder) {
  auto invalid_type_finder = [](absl::string_view /*type_url*/) { return nullptr; };

  EXPECT_THAT(ExtractStringFieldValue(*request_type_, invalid_type_finder, "bucket.ratio",
                                      test_request_raw_proto_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(StructUtilTest, RedactPaths_Basic) {
  std::string proto_struct_string = "fields {"
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
  CHECK(Protobuf::TextFormat::ParseFromString(proto_struct_string, &proto_struct));

  std::vector<std::string> paths_to_redact = {"nested.deeper_nest"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string = "fields {"
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
  CHECK(Protobuf::TextFormat::ParseFromString(expected_struct_string, &expected_struct));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(expected_struct, proto_struct));
}

TEST(StructUtilTest, RedactPaths_HandlesOneOfFields) {
  std::string proto_struct_string = "fields {"
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
  CHECK(Protobuf::TextFormat::ParseFromString(proto_struct_string, &proto_struct));

  std::vector<std::string> paths_to_redact = {"nested_value"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string = "fields {"
                                       "  key: \"nested_value\""
                                       "  value {"
                                       "    struct_value {}"
                                       "  }"
                                       "}";
  Struct expected_struct;
  CHECK(Protobuf::TextFormat::ParseFromString(expected_struct_string, &expected_struct));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(expected_struct, proto_struct));
}

TEST(StructUtilTest, RedactPaths_AllowsRepeatedLeafMessageType) {
  std::string proto_struct_string = "fields {"
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
  CHECK(Protobuf::TextFormat::ParseFromString(proto_struct_string, &proto_struct));

  std::vector<std::string> paths_to_redact = {"repeated_message_field"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string = "fields {"
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
  CHECK(Protobuf::TextFormat::ParseFromString(expected_struct_string, &expected_struct));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(expected_struct, proto_struct));
}

TEST(StructUtilTest, RedactPaths_AllowsRepeatedNonLeafMessageType) {
  std::string proto_struct_string = "fields {"
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
  CHECK(Protobuf::TextFormat::ParseFromString(proto_struct_string, &proto_struct));

  std::vector<std::string> paths_to_redact = {"repeated_message_field.deeper_nest"};
  RedactPaths(paths_to_redact, &proto_struct);

  std::string expected_struct_string = "fields {"
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
  CHECK(Protobuf::TextFormat::ParseFromString(expected_struct_string, &expected_struct));
  EXPECT_TRUE(Protobuf::util::MessageDifferencer::Equals(expected_struct, proto_struct));
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
  EXPECT_EQ(ExtractLocationIdFromResourceName(params.resource_name), params.expected_location);
}

INSTANTIATE_TEST_SUITE_P(
    ExtractLocationIdFromResourceNameTests, ExtractLocationIdFromResourceNameTest,
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
    [](const ::testing::TestParamInfo<ExtractLocationIdFromResourceNameTest::ParamType>& info) {
      return info.param.test_name;
    });

} // namespace
} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
