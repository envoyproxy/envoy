#include <unordered_set>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.validate.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(UtilityTest, RepeatedPtrUtilDebugString) {
  Protobuf::RepeatedPtrField<ProtobufWkt::UInt32Value> repeated;
  EXPECT_EQ("[]", RepeatedPtrUtil::debugString(repeated));
  repeated.Add()->set_value(10);
  EXPECT_EQ("[value: 10\n]", RepeatedPtrUtil::debugString(repeated));
  repeated.Add()->set_value(20);
  EXPECT_EQ("[value: 10\n, value: 20\n]", RepeatedPtrUtil::debugString(repeated));
}

TEST(UtilityTest, DowncastAndValidate) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  EXPECT_THROW(MessageUtil::validate(bootstrap), ProtoValidationException);
  EXPECT_THROW(
      MessageUtil::downcastAndValidate<const envoy::config::bootstrap::v2::Bootstrap&>(bootstrap),
      ProtoValidationException);
}

TEST(UtilityTest, LoadBinaryProtoFromFile) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  bootstrap.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address("1.1.1.1");

  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb", bootstrap.SerializeAsString());

  envoy::config::bootstrap::v2::Bootstrap proto_from_file;
  MessageUtil::loadFromFile(filename, proto_from_file);
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

TEST(UtilityTest, LoadTextProtoFromFile) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  bootstrap.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address("1.1.1.1");

  ProtobufTypes::String bootstrap_text;
  ASSERT_TRUE(Protobuf::TextFormat::PrintToString(bootstrap, &bootstrap_text));
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", bootstrap_text);

  envoy::config::bootstrap::v2::Bootstrap proto_from_file;
  MessageUtil::loadFromFile(filename, proto_from_file);
  EXPECT_TRUE(TestUtility::protoEqual(bootstrap, proto_from_file));
}

TEST(UtilityTest, LoadTextProtoFromFile_Failure) {
  const std::string filename =
      TestEnvironment::writeStringToFileForTest("proto.pb_text", "invalid {");

  envoy::config::bootstrap::v2::Bootstrap proto_from_file;
  EXPECT_THROW_WITH_MESSAGE(MessageUtil::loadFromFile(filename, proto_from_file), EnvoyException,
                            "Unable to parse file \"" + filename +
                                "\" as a text protobuf (type envoy.config.bootstrap.v2.Bootstrap)");
}

TEST(UtilityTest, KeyValueStruct) {
  const ProtobufWkt::Struct obj = MessageUtil::keyValueStruct("test_key", "test_value");
  EXPECT_EQ(obj.fields_size(), 1);
  EXPECT_EQ(obj.fields().at("test_key").kind_case(), ProtobufWkt::Value::KindCase::kStringValue);
  EXPECT_EQ(obj.fields().at("test_key").string_value(), "test_value");
}

TEST(UtilityTest, ValueUtilEqual_NullValues) {
  ProtobufWkt::Value v1, v2;
  v1.set_null_value(ProtobufWkt::NULL_VALUE);
  v2.set_null_value(ProtobufWkt::NULL_VALUE);

  ProtobufWkt::Value other;
  other.set_string_value("s");

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, other));
}

TEST(UtilityTest, ValueUtilEqual_StringValues) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("s");
  v2.set_string_value("s");
  v3.set_string_value("not_s");

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
}

TEST(UtilityTest, ValueUtilEqual_NumberValues) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_number_value(1.0);
  v2.set_number_value(1.0);
  v3.set_number_value(100.0);

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
}

TEST(UtilityTest, ValueUtilEqual_BoolValues) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_bool_value(true);
  v2.set_bool_value(true);
  v3.set_bool_value(false);

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
}

TEST(UtilityTest, ValueUtilEqual_StructValues) {
  ProtobufWkt::Value string_val1, string_val2, bool_val;

  string_val1.set_string_value("s1");
  string_val2.set_string_value("s2");
  bool_val.set_bool_value(true);

  ProtobufWkt::Value v1, v2, v3, v4;
  v1.mutable_struct_value()->mutable_fields()->insert({"f1", string_val1});
  v1.mutable_struct_value()->mutable_fields()->insert({"f2", bool_val});

  v2.mutable_struct_value()->mutable_fields()->insert({"f1", string_val1});
  v2.mutable_struct_value()->mutable_fields()->insert({"f2", bool_val});

  v3.mutable_struct_value()->mutable_fields()->insert({"f1", string_val2});
  v3.mutable_struct_value()->mutable_fields()->insert({"f2", bool_val});

  v4.mutable_struct_value()->mutable_fields()->insert({"f1", string_val1});

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
  EXPECT_FALSE(ValueUtil::equal(v1, v4));
}

TEST(UtilityTest, ValueUtilEqual_ListValues) {
  ProtobufWkt::Value v1, v2, v3, v4;
  v1.mutable_list_value()->add_values()->set_string_value("s");
  v1.mutable_list_value()->add_values()->set_bool_value(true);

  v2.mutable_list_value()->add_values()->set_string_value("s");
  v2.mutable_list_value()->add_values()->set_bool_value(true);

  v3.mutable_list_value()->add_values()->set_bool_value(true);
  v3.mutable_list_value()->add_values()->set_string_value("s");

  v4.mutable_list_value()->add_values()->set_string_value("s");

  EXPECT_TRUE(ValueUtil::equal(v1, v2));
  EXPECT_FALSE(ValueUtil::equal(v1, v3));
  EXPECT_FALSE(ValueUtil::equal(v1, v4));
}

TEST(UtilityTest, ValueUtilHash) {
  ProtobufWkt::Value v;
  v.set_string_value("s1");

  EXPECT_NE(ValueUtil::hash(v), 0);
}

TEST(UtilityTest, HashedValue) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("s");
  v2.set_string_value("s");
  v3.set_string_value("not_s");

  HashedValue hv1(v1), hv2(v2), hv3(v3);

  EXPECT_EQ(hv1, hv2);
  EXPECT_NE(hv1, hv3);

  HashedValue copy(hv1);
  EXPECT_EQ(hv1, copy);
}

TEST(UtilityTest, HashedValueStdHash) {
  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("s");
  v2.set_string_value("s");
  v3.set_string_value("not_s");

  HashedValue hv1(v1), hv2(v2), hv3(v3);

  std::unordered_set<HashedValue> set;
  set.emplace(hv1);
  set.emplace(hv2);
  set.emplace(hv3);

  EXPECT_EQ(set.size(), 2); // hv1 == hv2
  EXPECT_NE(set.find(hv1), set.end());
  EXPECT_NE(set.find(hv3), set.end());
}

TEST(UtilityTest, JsonConvertSuccess) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Duration dest_duration;
  MessageUtil::jsonConvert(source_duration, dest_duration);
  EXPECT_EQ(42, dest_duration.seconds());
}

TEST(UtilityTest, JsonConvertFail) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(-281474976710656);
  ProtobufWkt::Duration dest_duration;
  EXPECT_THROW_WITH_REGEX(MessageUtil::jsonConvert(source_duration, dest_duration), EnvoyException,
                          "Unable to convert protobuf message to JSON string.*"
                          "seconds exceeds limit for field:  seconds: -281474976710656\n");
}

TEST(DurationUtilTest, OutOfRange) {
  {
    ProtobufWkt::Duration duration;
    duration.set_seconds(-1);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
  {
    ProtobufWkt::Duration duration;
    duration.set_nanos(-1);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
  {
    ProtobufWkt::Duration duration;
    duration.set_nanos(1000000000);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
  {
    ProtobufWkt::Duration duration;
    duration.set_seconds(Protobuf::util::TimeUtil::kDurationMaxSeconds + 1);
    EXPECT_THROW(DurationUtil::durationToMilliseconds(duration), DurationUtil::OutOfRangeException);
  }
}

} // namespace Envoy
