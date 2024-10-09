#include "source/common/json/json_utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

std::string toJson(const ProtobufWkt::Value& v) {
  std::string json_string;
  Utility::appendValueToString(v, json_string);
  return json_string;
}

TEST(JsonUtilityTest, AppendValueToString) {
  ProtobufWkt::Value v;

  // null
  EXPECT_EQ(toJson(v), "null");

  v.set_null_value(ProtobufWkt::NULL_VALUE);
  EXPECT_EQ(toJson(v), "null");

  // bool
  v.set_bool_value(true);
  EXPECT_EQ(toJson(v), "true");

  v.set_bool_value(false);
  EXPECT_EQ(toJson(v), "false");

  // number
  v.set_number_value(1);
  EXPECT_EQ(toJson(v), "1");

  v.set_number_value(1.1);
  EXPECT_EQ(toJson(v), "1.1");

  // string
  v.set_string_value("foo");
  EXPECT_EQ(toJson(v), "\"foo\"");

  // struct
  auto* struct_value = v.mutable_struct_value();
  EXPECT_EQ(toJson(v), R"EOF({})EOF");

  struct_value->mutable_fields()->insert({"foo", ValueUtil::stringValue("bar")});
  EXPECT_EQ(toJson(v), R"EOF({"foo":"bar"})EOF");

  // list
  auto* list_value = v.mutable_list_value();

  EXPECT_EQ(toJson(v), R"EOF([])EOF");

  list_value->add_values()->set_string_value("foo");
  list_value->add_values()->set_string_value("bar");

  EXPECT_EQ(toJson(v), R"EOF(["foo","bar"])EOF");

  // Complex structure
  const std::string yaml = R"EOF(
    a:
      a:
        - a: 1
          b: 2
      b:
        - a: 3
          b: 4
        - a: 5
      c: true
      d: [5, 3.14]
      e: foo
      f: 1.1
    b: [1, 2, 3]
    c: bar
    )EOF";

  MessageUtil::loadFromYaml(yaml, v, ProtobufMessage::getNullValidationVisitor());

  EXPECT_EQ(
      toJson(v),
      R"EOF({"a":{"a":[{"a":1,"b":2}],"b":[{"a":3,"b":4},{"a":5}],"c":true,"d":[5,"3.14"],"e":"foo","f":"1.1"},"b":[1,2,3],"c":"bar"})EOF");
}

} // namespace
} // namespace Json
} // namespace Envoy
