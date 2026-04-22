#include "source/extensions/filters/common/lua/protobuf_converter.h"

#include "test/common/protobuf/deterministic_hash_test.pb.h"
#include "test/extensions/filters/common/lua/lua_wrappers.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {
namespace {

class LuaProtobufConverterTest : public testing::Test {
public:
  LuaProtobufConverterTest()
      : tls_(std::make_shared<NiceMock<ThreadLocal::MockInstance>>()),
        state_(std::make_unique<ThreadLocalState>("function dummy() end", *tls_)) {}

protected:
  void SetUp() override {
    coroutine_ = state_->createCoroutine();
    lua_state_ = coroutine_->luaState();
  }

  // Helper function to get a value from a field message table
  std::string getFieldValue(const std::string& field_name, const std::string& value_type) {
    lua_getfield(lua_state_, -1, "fields");
    EXPECT_TRUE(lua_istable(lua_state_, -1)) << "fields should be a table";

    lua_getfield(lua_state_, -1, field_name.c_str());
    EXPECT_TRUE(lua_istable(lua_state_, -1)) << field_name << " should be a table";

    lua_getfield(lua_state_, -1, value_type.c_str());

    std::string result;
    switch (lua_type(lua_state_, -1)) {
    case LUA_TSTRING:
      result = lua_tostring(lua_state_, -1);
      break;
    case LUA_TNUMBER:
      result = std::to_string(static_cast<int>(lua_tonumber(lua_state_, -1)));
      break;
    case LUA_TBOOLEAN:
      result = std::to_string(lua_toboolean(lua_state_, -1));
      break;
    }
    lua_pop(lua_state_, 3);
    return result;
  }

  // Helper function to get a value from a nested message
  std::string getNestedFieldValue(const std::string& field_name, const std::string& sub_field_name,
                                  const std::string& value_type) {
    lua_getfield(lua_state_, -1, "fields");
    EXPECT_TRUE(lua_istable(lua_state_, -1)) << "fields should be a table";

    lua_getfield(lua_state_, -1, field_name.c_str());
    EXPECT_TRUE(lua_istable(lua_state_, -1)) << field_name << " should be a table";

    lua_getfield(lua_state_, -1, "struct_value");
    EXPECT_TRUE(lua_istable(lua_state_, -1)) << "struct_value should be a table";

    lua_getfield(lua_state_, -1, "fields");
    EXPECT_TRUE(lua_istable(lua_state_, -1)) << "nested fields should be a table";

    lua_getfield(lua_state_, -1, sub_field_name.c_str());
    EXPECT_TRUE(lua_istable(lua_state_, -1)) << sub_field_name << " should be a table";

    lua_getfield(lua_state_, -1, value_type.c_str());

    std::string result;
    switch (lua_type(lua_state_, -1)) {
    case LUA_TSTRING:
      result = lua_tostring(lua_state_, -1);
      break;
    case LUA_TNUMBER:
      result = std::to_string(static_cast<int>(lua_tonumber(lua_state_, -1)));
      break;
    case LUA_TBOOLEAN:
      result = std::to_string(lua_toboolean(lua_state_, -1));
      break;
    }
    lua_pop(lua_state_, 6);
    return result;
  }

  std::shared_ptr<NiceMock<ThreadLocal::MockInstance>> tls_;
  std::unique_ptr<ThreadLocalState> state_;
  CoroutinePtr coroutine_;
  lua_State* lua_state_{};
};

TEST_F(LuaProtobufConverterTest, BasicTypes) {
  const std::string yaml = R"EOF(
    filter_metadata:
      envoy.test:
        string_field: "hello"
        double_field: 123.456
        bool_field: true
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  lua_getfield(lua_state_, -1, "envoy.test");

  EXPECT_EQ(getFieldValue("string_field", "string_value"), "hello");
  EXPECT_EQ(getFieldValue("bool_field", "bool_value"), "1");

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, NestedMessage) {
  const std::string yaml = R"EOF(
    filter_metadata:
      envoy.test:
        nested:
          field1: "value1"
          field2: 123
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  lua_getfield(lua_state_, -1, "envoy.test");

  EXPECT_EQ(getNestedFieldValue("nested", "field1", "string_value"), "value1");
  EXPECT_EQ(getNestedFieldValue("nested", "field2", "number_value"), "123");

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, MultipleNestedMessages) {
  const std::string yaml = R"EOF(
    filter_metadata:
      envoy.test:
        nested1:
          field1: "value1"
          field2: 123
        nested2:
          field3: true
          field4: "value4"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  lua_getfield(lua_state_, -1, "envoy.test");

  EXPECT_EQ(getNestedFieldValue("nested1", "field1", "string_value"), "value1");
  EXPECT_EQ(getNestedFieldValue("nested1", "field2", "number_value"), "123");
  EXPECT_EQ(getNestedFieldValue("nested2", "field3", "bool_value"), "1");
  EXPECT_EQ(getNestedFieldValue("nested2", "field4", "string_value"), "value4");

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, DeepNestedMessage) {
  const std::string yaml = R"EOF(
    filter_metadata:
      envoy.test:
        level1:
          level2:
            level3:
              field1: "deep value"
              field2: 999
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  lua_getfield(lua_state_, -1, "envoy.test");

  // We need to navigate through the levels
  lua_getfield(lua_state_, -1, "fields");
  lua_getfield(lua_state_, -1, "level1");
  lua_getfield(lua_state_, -1, "struct_value");
  lua_getfield(lua_state_, -1, "fields");
  lua_getfield(lua_state_, -1, "level2");
  lua_getfield(lua_state_, -1, "struct_value");
  lua_getfield(lua_state_, -1, "fields");
  lua_getfield(lua_state_, -1, "level3");
  lua_getfield(lua_state_, -1, "struct_value");

  EXPECT_EQ(getFieldValue("field1", "string_value"), "deep value");
  EXPECT_EQ(getFieldValue("field2", "number_value"), "999");

  lua_pop(lua_state_, 13);
}

TEST_F(LuaProtobufConverterTest, EmptyMessage) {
  const std::string yaml = R"EOF(
    filter_metadata:
      envoy.test: {}
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "filter_metadata");
  ASSERT_TRUE(lua_istable(lua_state_, -1));

  lua_getfield(lua_state_, -1, "envoy.test");
  ASSERT_TRUE(lua_istable(lua_state_, -1));

  lua_pushnil(lua_state_);
  int count = 0;
  while (lua_next(lua_state_, -2) != 0) {
    count++;
    lua_pop(lua_state_, 1);
  }
  EXPECT_EQ(count, 0);

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, MultipleNamespaces) {
  const std::string yaml = R"EOF(
    filter_metadata:
      envoy.test1:
        field1: "value1"
      envoy.test2:
        field2: "value2"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");

  // Check first namespace
  lua_getfield(lua_state_, -1, "envoy.test1");
  EXPECT_EQ(getFieldValue("field1", "string_value"), "value1");
  lua_pop(lua_state_, 1);

  // Check second namespace
  lua_getfield(lua_state_, -1, "envoy.test2");
  EXPECT_EQ(getFieldValue("field2", "string_value"), "value2");

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, NumericTypes) {
  envoy::config::core::v3::Metadata metadata;
  auto* filter_metadata = metadata.mutable_filter_metadata();
  auto& struct_value = (*filter_metadata)["envoy.test"];
  auto* fields = struct_value.mutable_fields();

  // Test all numeric types to hit all switch cases
  (*fields)["int32_field"].set_number_value(42);         // Small positive int32
  (*fields)["int32_max"].set_number_value(2147483647);   // Max int32
  (*fields)["int64_field"].set_number_value(1234567890); // Medium int64

  // Use smaller values for uint tests to avoid overflow
  (*fields)["uint32_field"].set_number_value(1000000); // Small uint32
  (*fields)["uint64_field"].set_number_value(2000000); // Small uint64

  (*fields)["double_field"].set_number_value(3.14159); // Double with decimals
  (*fields)["float_field"].set_number_value(2.71828);  // Float with decimals
  (*fields)["enum_field"].set_number_value(2);         // Enum value
  (*fields)["default_field"];                          // Default case

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  lua_getfield(lua_state_, -1, "envoy.test");

  // Verify each type of field
  EXPECT_EQ(getFieldValue("int32_field", "number_value"), "42");
  EXPECT_EQ(getFieldValue("int32_max", "number_value"), "2147483647");
  EXPECT_EQ(getFieldValue("int64_field", "number_value"), "1234567890");
  EXPECT_EQ(getFieldValue("uint32_field", "number_value"), "1000000");
  EXPECT_EQ(getFieldValue("uint64_field", "number_value"), "2000000");
  EXPECT_EQ(getFieldValue("double_field", "number_value"),
            "3"); // Integer conversion in getFieldValue
  EXPECT_EQ(getFieldValue("float_field", "number_value"),
            "2"); // Integer conversion in getFieldValue
  EXPECT_EQ(getFieldValue("enum_field", "number_value"), "2");

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, EnumType) {
  envoy::config::core::v3::Metadata metadata;
  auto* filter_metadata = metadata.mutable_filter_metadata();
  auto& struct_value = (*filter_metadata)["envoy.test"];

  auto* fields = struct_value.mutable_fields();
  (*fields)["enum_field"].set_number_value(2); // Using 2 as an example enum value

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  lua_getfield(lua_state_, -1, "envoy.test");

  EXPECT_EQ(getFieldValue("enum_field", "number_value"), "2");

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, RepeatedFields) {
  envoy::config::core::v3::Metadata metadata;
  auto* filter_metadata = metadata.mutable_filter_metadata();
  auto* test_metadata = &(*filter_metadata)["envoy.test"];
  auto* fields = test_metadata->mutable_fields();

  // Create repeated string field
  auto* string_list_value = (*fields)["string_list"].mutable_list_value();
  string_list_value->add_values()->set_string_value("value1");
  string_list_value->add_values()->set_string_value("value2");

  // Create repeated number field
  auto* number_list_value = (*fields)["number_list"].mutable_list_value();
  number_list_value->add_values()->set_number_value(1);
  number_list_value->add_values()->set_number_value(2);

  // Create repeated bool field
  auto* bool_list_value = (*fields)["bool_list"].mutable_list_value();
  bool_list_value->add_values()->set_bool_value(true);
  bool_list_value->add_values()->set_bool_value(false);

  // Create repeated nested message field
  auto* msg_list_value = (*fields)["nested_list"].mutable_list_value();

  // First nested message
  auto* msg1 = msg_list_value->add_values()->mutable_struct_value();
  (*msg1->mutable_fields())["name"].set_string_value("first");
  (*msg1->mutable_fields())["value"].set_number_value(100);
  auto* sub_msg1 = (*msg1->mutable_fields())["nested"].mutable_struct_value();
  (*sub_msg1->mutable_fields())["flag"].set_bool_value(true);

  // Second nested message
  auto* msg2 = msg_list_value->add_values()->mutable_struct_value();
  (*msg2->mutable_fields())["name"].set_string_value("second");
  (*msg2->mutable_fields())["value"].set_number_value(200);
  auto* sub_msg2 = (*msg2->mutable_fields())["nested"].mutable_struct_value();
  (*sub_msg2->mutable_fields())["flag"].set_bool_value(false);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "envoy.test");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  // Test string list
  lua_getfield(lua_state_, -1, "string_list");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "list_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "values");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "string_value");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "value1");
  lua_pop(lua_state_, 2);
  lua_pop(lua_state_, 3);

  // Test nested message list
  lua_getfield(lua_state_, -1, "nested_list");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "list_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "values");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  // Check first nested message
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  lua_getfield(lua_state_, -1, "name");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "string_value");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "first");
  lua_pop(lua_state_, 2);

  lua_getfield(lua_state_, -1, "nested");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "flag");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "bool_value");
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 5);
  lua_pop(lua_state_, 3);

  lua_pop(lua_state_, 3);
  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, UncoveredRepeatedFields) {
  envoy::config::core::v3::Metadata metadata;
  auto* filter_metadata = metadata.mutable_filter_metadata();
  auto* test_metadata = &(*filter_metadata)["envoy.test"];
  auto* fields = test_metadata->mutable_fields();

  // Repeated INT32/INT64
  auto* repeated_int = (*fields)["repeated_int"].mutable_list_value();
  repeated_int->add_values()->set_number_value(42);         // INT32
  repeated_int->add_values()->set_number_value(1234567890); // INT64

  // Repeated BOOL
  auto* repeated_bool = (*fields)["repeated_bool"].mutable_list_value();
  repeated_bool->add_values()->set_bool_value(true);
  repeated_bool->add_values()->set_bool_value(false);

  // Add a field that will hit the default case
  auto* default_case = (*fields)["default_case"].mutable_list_value();
  default_case->add_values(); // Empty value to trigger default case

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "envoy.test");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  // Test repeated int
  lua_getfield(lua_state_, -1, "repeated_int");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "list_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "values");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "number_value");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 42);
  lua_pop(lua_state_, 2);

  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "number_value");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 1234567890);
  lua_pop(lua_state_, 5);

  // Test repeated bool
  lua_getfield(lua_state_, -1, "repeated_bool");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "list_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "values");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "bool_value");
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 2);

  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "bool_value");
  EXPECT_FALSE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 5);

  // Test default case
  lua_getfield(lua_state_, -1, "default_case");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "list_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "values");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_pop(lua_state_, 4);

  lua_pop(lua_state_, 3);
}

TEST_F(LuaProtobufConverterTest, MapFields) {
  envoy::config::core::v3::Metadata metadata;
  auto* filter_metadata = metadata.mutable_filter_metadata();
  auto* test_metadata = &(*filter_metadata)["envoy.test"];
  auto* fields = test_metadata->mutable_fields();

  // 1. String key maps (covering all value types)
  auto* str_map = (*fields)["string_key_map"].mutable_struct_value();
  (*str_map->mutable_fields())["str_val"].set_string_value("value1");
  (*str_map->mutable_fields())["num_val"].set_number_value(100);
  (*str_map->mutable_fields())["bool_val"].set_bool_value(true);
  auto* nested1 = (*str_map->mutable_fields())["msg_val"].mutable_struct_value();
  (*nested1->mutable_fields())["inner"].set_string_value("nested1");
  (*nested1->mutable_fields())["value"].set_number_value(42);

  // 2. Integer key maps (covering all value types)
  auto* int_map = (*fields)["int_key_map"].mutable_struct_value();
  (*int_map->mutable_fields())["1"].set_string_value("str_from_int");
  (*int_map->mutable_fields())["2"].set_number_value(200);
  (*int_map->mutable_fields())["3"].set_bool_value(true);
  auto* nested2 = (*int_map->mutable_fields())["4"].mutable_struct_value();
  (*nested2->mutable_fields())["name"].set_string_value("msg_from_int");
  (*nested2->mutable_fields())["value"].set_number_value(300);

  // 3. Deep nested map for complete coverage
  auto* deep_map = (*fields)["nested_map"].mutable_struct_value();
  auto* deep_msg = (*deep_map->mutable_fields())["key1"].mutable_struct_value();
  (*deep_msg->mutable_fields())["name"].set_string_value("deep");
  (*deep_msg->mutable_fields())["value"].set_number_value(500);
  auto* sub_msg = (*deep_msg->mutable_fields())["sub"].mutable_struct_value();
  (*sub_msg->mutable_fields())["flag"].set_bool_value(true);
  (*sub_msg->mutable_fields())["count"].set_number_value(42);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, metadata);

  lua_getfield(lua_state_, -1, "filter_metadata");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "envoy.test");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  // Test string key map with all value types
  lua_getfield(lua_state_, -1, "string_key_map");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  // Check string value
  lua_getfield(lua_state_, -1, "str_val");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "string_value");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "value1");
  lua_pop(lua_state_, 2);

  // Check number value
  lua_getfield(lua_state_, -1, "num_val");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "number_value");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 100);
  lua_pop(lua_state_, 2);

  // Check bool value
  lua_getfield(lua_state_, -1, "bool_val");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "bool_value");
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 2);

  // Check nested message with value field
  lua_getfield(lua_state_, -1, "msg_val");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "number_value");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 42);
  lua_pop(lua_state_, 8);

  // Test integer key map
  lua_getfield(lua_state_, -1, "int_key_map");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  // Check string value with integer key
  lua_getfield(lua_state_, -1, "1");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "string_value");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "str_from_int");
  lua_pop(lua_state_, 2);

  // Check number value with integer key
  lua_getfield(lua_state_, -1, "2");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "number_value");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 200);
  lua_pop(lua_state_, 2);

  // Check bool value with integer key
  lua_getfield(lua_state_, -1, "3");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "bool_value");
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 2);

  // Check nested message with integer key
  lua_getfield(lua_state_, -1, "4");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "name");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "string_value");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "msg_from_int");
  lua_pop(lua_state_, 2);
  lua_getfield(lua_state_, -1, "value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "number_value");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 300);
  lua_pop(lua_state_, 8);

  // Test deep nested map
  lua_getfield(lua_state_, -1, "nested_map");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "key1");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  // Check name and value fields
  lua_getfield(lua_state_, -1, "name");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "string_value");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "deep");
  lua_pop(lua_state_, 2);
  lua_getfield(lua_state_, -1, "value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "number_value");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 500);
  lua_pop(lua_state_, 2);

  // Check sub message
  lua_getfield(lua_state_, -1, "sub");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "struct_value");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "fields");
  EXPECT_TRUE(lua_istable(lua_state_, -1));

  lua_getfield(lua_state_, -1, "flag");
  EXPECT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "bool_value");
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 12);

  // Clean up
  lua_pop(lua_state_, 3);
}

// Add comprehensive native protobuf message tests
TEST_F(LuaProtobufConverterTest, NativeProtobufSingleFieldsAllTypes) {
  deterministichashtest::SingleFields message;

  // Set all field types
  message.set_b(true);
  message.set_string("test_string");
  message.set_int32(-123);
  message.set_uint32(456);
  message.set_int64(-789012345);
  message.set_uint64(987654321);
  message.set_bytes("binary_data");
  message.set_db(3.14159);
  message.set_f(2.718f);
  message.set_e(deterministichashtest::FOO);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, message);

  ASSERT_TRUE(lua_istable(lua_state_, -1));

  // Test bool field
  lua_getfield(lua_state_, -1, "b");
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 1);

  // Test string field
  lua_getfield(lua_state_, -1, "string");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "test_string");
  lua_pop(lua_state_, 1);

  // Test int32 field
  lua_getfield(lua_state_, -1, "int32");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), -123);
  lua_pop(lua_state_, 1);

  // Test uint32 field
  lua_getfield(lua_state_, -1, "uint32");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 456);
  lua_pop(lua_state_, 1);

  // Test int64 field
  lua_getfield(lua_state_, -1, "int64");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), -789012345);
  lua_pop(lua_state_, 1);

  // Test uint64 field
  lua_getfield(lua_state_, -1, "uint64");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 987654321);
  lua_pop(lua_state_, 1);

  // Test bytes field
  lua_getfield(lua_state_, -1, "bytes");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "binary_data");
  lua_pop(lua_state_, 1);

  // Test double field
  lua_getfield(lua_state_, -1, "db");
  EXPECT_NEAR(lua_tonumber(lua_state_, -1), 3.14159, 0.00001);
  lua_pop(lua_state_, 1);

  // Test float field
  lua_getfield(lua_state_, -1, "f");
  EXPECT_NEAR(lua_tonumber(lua_state_, -1), 2.718, 0.001);
  lua_pop(lua_state_, 1);

  // Test enum field
  lua_getfield(lua_state_, -1, "e");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), deterministichashtest::FOO);
  lua_pop(lua_state_, 1);

  lua_pop(lua_state_, 1);
}

TEST_F(LuaProtobufConverterTest, NativeProtobufRepeatedFieldsAllTypes) {
  deterministichashtest::RepeatedFields message;

  // Add repeated bool values
  message.add_bools(true);
  message.add_bools(false);

  // Add repeated string values
  message.add_strings("first");
  message.add_strings("second");

  // Add repeated int32 values
  message.add_int32s(-100);
  message.add_int32s(200);

  // Add repeated uint32 values
  message.add_uint32s(300);
  message.add_uint32s(400);

  // Add repeated int64 values
  message.add_int64s(-500000000);
  message.add_int64s(600000000);

  // Add repeated uint64 values
  message.add_uint64s(700000000);
  message.add_uint64s(800000000);

  // Add repeated bytes values
  message.add_byteses("bytes1");
  message.add_byteses("bytes2");

  // Add repeated double values
  message.add_doubles(1.23);
  message.add_doubles(4.56);

  // Add repeated float values
  message.add_floats(7.89f);
  message.add_floats(10.11f);

  // Add repeated enum values
  message.add_enums(deterministichashtest::BAR);
  message.add_enums(deterministichashtest::ZERO);

  // Add repeated message values
  auto* sub_msg1 = message.add_messages();
  sub_msg1->set_index(111);
  auto* sub_msg2 = message.add_messages();
  sub_msg2->set_index(222);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, message);

  ASSERT_TRUE(lua_istable(lua_state_, -1));

  // Test repeated bools
  lua_getfield(lua_state_, -1, "bools");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_FALSE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 2);

  // Test repeated strings
  lua_getfield(lua_state_, -1, "strings");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "first");
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "second");
  lua_pop(lua_state_, 2);

  // Test repeated int32s
  lua_getfield(lua_state_, -1, "int32s");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), -100);
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 200);
  lua_pop(lua_state_, 2);

  // Test repeated uint32s
  lua_getfield(lua_state_, -1, "uint32s");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 300);
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 400);
  lua_pop(lua_state_, 2);

  // Test repeated int64s
  lua_getfield(lua_state_, -1, "int64s");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), -500000000);
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 600000000);
  lua_pop(lua_state_, 2);

  // Test repeated uint64s
  lua_getfield(lua_state_, -1, "uint64s");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 700000000);
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 800000000);
  lua_pop(lua_state_, 2);

  // Test repeated bytes
  lua_getfield(lua_state_, -1, "byteses");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "bytes1");
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "bytes2");
  lua_pop(lua_state_, 2);

  // Test repeated doubles
  lua_getfield(lua_state_, -1, "doubles");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_NEAR(lua_tonumber(lua_state_, -1), 1.23, 0.001);
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_NEAR(lua_tonumber(lua_state_, -1), 4.56, 0.001);
  lua_pop(lua_state_, 2);

  // Test repeated floats
  lua_getfield(lua_state_, -1, "floats");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_NEAR(lua_tonumber(lua_state_, -1), 7.89, 0.001);
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_NEAR(lua_tonumber(lua_state_, -1), 10.11, 0.001);
  lua_pop(lua_state_, 2);

  // Test repeated enums
  lua_getfield(lua_state_, -1, "enums");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), deterministichashtest::BAR);
  lua_pop(lua_state_, 1);
  lua_rawgeti(lua_state_, -1, 2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), deterministichashtest::ZERO);
  lua_pop(lua_state_, 2);

  // Test repeated messages
  lua_getfield(lua_state_, -1, "messages");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_rawgeti(lua_state_, -1, 1);
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "index");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 111);
  lua_pop(lua_state_, 2);
  lua_rawgeti(lua_state_, -1, 2);
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "index");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 222);
  lua_pop(lua_state_, 3);

  lua_pop(lua_state_, 1);
}

TEST_F(LuaProtobufConverterTest, NativeProtobufMapFieldsAllKeyTypes) {
  deterministichashtest::Maps message;

  // Test map<bool, string>
  (*message.mutable_bool_string())[true] = "true_value";
  (*message.mutable_bool_string())[false] = "false_value";

  // Test map<string, bool>
  (*message.mutable_string_bool())["key1"] = true;
  (*message.mutable_string_bool())["key2"] = false;

  // Test map<int32, uint32>
  (*message.mutable_int32_uint32())[-100] = 200;
  (*message.mutable_int32_uint32())[300] = 400;

  // Test map<uint32, int32>
  (*message.mutable_uint32_int32())[500] = -600;
  (*message.mutable_uint32_int32())[700] = 800;

  // Test map<int64, uint64>
  (*message.mutable_int64_uint64())[-900000000] = 1000000000;
  (*message.mutable_int64_uint64())[1100000000] = 1200000000;

  // Test map<uint64, string>
  (*message.mutable_uint64_int64())[1300000000] = "large_value";
  (*message.mutable_uint64_int64())[1400000000] = "another_large_value";

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, message);

  ASSERT_TRUE(lua_istable(lua_state_, -1));

  // Test bool key map
  lua_getfield(lua_state_, -1, "bool_string");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_pushboolean(lua_state_, true);
  lua_gettable(lua_state_, -2);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "true_value");
  lua_pop(lua_state_, 1);
  lua_pushboolean(lua_state_, false);
  lua_gettable(lua_state_, -2);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "false_value");
  lua_pop(lua_state_, 2);

  // Test string key map
  lua_getfield(lua_state_, -1, "string_bool");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "key1");
  EXPECT_TRUE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 1);
  lua_getfield(lua_state_, -1, "key2");
  EXPECT_FALSE(lua_toboolean(lua_state_, -1));
  lua_pop(lua_state_, 2);

  // Test int32 key map
  lua_getfield(lua_state_, -1, "int32_uint32");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_pushnumber(lua_state_, -100);
  lua_gettable(lua_state_, -2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 200);
  lua_pop(lua_state_, 1);
  lua_pushnumber(lua_state_, 300);
  lua_gettable(lua_state_, -2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 400);
  lua_pop(lua_state_, 2);

  // Test uint32 key map
  lua_getfield(lua_state_, -1, "uint32_int32");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_pushnumber(lua_state_, 500);
  lua_gettable(lua_state_, -2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), -600);
  lua_pop(lua_state_, 1);
  lua_pushnumber(lua_state_, 700);
  lua_gettable(lua_state_, -2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 800);
  lua_pop(lua_state_, 2);

  // Test int64 key map
  lua_getfield(lua_state_, -1, "int64_uint64");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_pushnumber(lua_state_, -900000000);
  lua_gettable(lua_state_, -2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 1000000000);
  lua_pop(lua_state_, 1);
  lua_pushnumber(lua_state_, 1100000000);
  lua_gettable(lua_state_, -2);
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 1200000000);
  lua_pop(lua_state_, 2);

  // Test uint64 key map
  lua_getfield(lua_state_, -1, "uint64_int64");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_pushnumber(lua_state_, 1300000000);
  lua_gettable(lua_state_, -2);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "large_value");
  lua_pop(lua_state_, 1);
  lua_pushnumber(lua_state_, 1400000000);
  lua_gettable(lua_state_, -2);
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "another_large_value");
  lua_pop(lua_state_, 2);

  lua_pop(lua_state_, 1);
}

TEST_F(LuaProtobufConverterTest, EmptyFieldsAndMissingFields) {
  deterministichashtest::SingleFields message;
  // Only set some fields, leave others unset to test missing field logic
  message.set_string("only_set_string");
  message.set_int32(42);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, message);

  ASSERT_TRUE(lua_istable(lua_state_, -1));

  // Check that set fields exist
  lua_getfield(lua_state_, -1, "string");
  EXPECT_STREQ(lua_tostring(lua_state_, -1), "only_set_string");
  lua_pop(lua_state_, 1);

  lua_getfield(lua_state_, -1, "int32");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 42);
  lua_pop(lua_state_, 1);

  // Check that unset fields are not present (should be nil)
  lua_getfield(lua_state_, -1, "b");
  EXPECT_TRUE(lua_isnil(lua_state_, -1));
  lua_pop(lua_state_, 1);

  lua_getfield(lua_state_, -1, "uint64");
  EXPECT_TRUE(lua_isnil(lua_state_, -1));
  lua_pop(lua_state_, 1);

  lua_pop(lua_state_, 1);
}

TEST_F(LuaProtobufConverterTest, EmptyRepeatedFields) {
  deterministichashtest::RepeatedFields message;
  // Don't add any repeated field values to test empty repeated field logic

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, message);

  ASSERT_TRUE(lua_istable(lua_state_, -1));

  // Count the number of fields in the table - should be 0 for empty repeated fields
  lua_pushnil(lua_state_);
  int field_count = 0;
  while (lua_next(lua_state_, -2) != 0) {
    field_count++;
    lua_pop(lua_state_, 1);
  }
  EXPECT_EQ(field_count, 0);

  lua_pop(lua_state_, 1);
}

TEST_F(LuaProtobufConverterTest, EmptyMapFields) {
  deterministichashtest::Maps message;
  // Don't add any map values to test empty map field logic

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, message);

  ASSERT_TRUE(lua_istable(lua_state_, -1));

  // Count the number of fields in the table - should be 0 for empty maps
  lua_pushnil(lua_state_);
  int field_count = 0;
  while (lua_next(lua_state_, -2) != 0) {
    field_count++;
    lua_pop(lua_state_, 1);
  }
  EXPECT_EQ(field_count, 0);

  lua_pop(lua_state_, 1);
}

TEST_F(LuaProtobufConverterTest, NestedMessageWithRecursion) {
  deterministichashtest::Recursion message;
  message.set_index(1);

  // Create a nested child
  auto* child = message.mutable_child();
  child->set_index(2);

  // Create a grandchild
  auto* grandchild = child->mutable_child();
  grandchild->set_index(3);

  ProtobufConverterUtils::pushLuaTableFromMessage(lua_state_, message);

  ASSERT_TRUE(lua_istable(lua_state_, -1));

  // Check root level
  lua_getfield(lua_state_, -1, "index");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 1);
  lua_pop(lua_state_, 1);

  // Check child level
  lua_getfield(lua_state_, -1, "child");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "index");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 2);
  lua_pop(lua_state_, 1);

  // Check grandchild level
  lua_getfield(lua_state_, -1, "child");
  ASSERT_TRUE(lua_istable(lua_state_, -1));
  lua_getfield(lua_state_, -1, "index");
  EXPECT_EQ(lua_tonumber(lua_state_, -1), 3);
  lua_pop(lua_state_, 1);

  // Grandchild should not have a child (unset field)
  lua_getfield(lua_state_, -1, "child");
  EXPECT_TRUE(lua_isnil(lua_state_, -1));
  lua_pop(lua_state_, 3);

  lua_pop(lua_state_, 1);
}

TEST_F(LuaProtobufConverterTest, TypeURLEndingWithSlash) {
  // Create Any with type URL ending with slash and no type name after slash
  Protobuf::Any any_message;
  any_message.set_type_url("type.googleapis.com/");

  Protobuf::Map<std::string, Protobuf::Any> typed_metadata_map;
  typed_metadata_map["test.filter"] = any_message;

  // Push dummy value at index 1, then filter name at index 2 (function expects index 2)
  lua_pushnil(lua_state_);
  lua_pushstring(lua_state_, "test.filter");

  int result = ProtobufConverterUtils::processDynamicTypedMetadataFromLuaCall(lua_state_,
                                                                              typed_metadata_map);

  EXPECT_EQ(result, 1);
  EXPECT_TRUE(lua_isnil(lua_state_, -1));
  lua_pop(lua_state_, 3); // Pop result + the 2 values we pushed
}

TEST_F(LuaProtobufConverterTest, PrototypeNotFound) {
  // Create Any with a valid but invalid message type
  // Using a well-known type that exists in descriptor pool but might not have a prototype
  Protobuf::Any any_message;
  any_message.set_type_url("type.googleapis.com/google.protobuf.FileDescriptorSet");
  any_message.set_value("dummy_data");

  Protobuf::Map<std::string, Protobuf::Any> typed_metadata_map;
  typed_metadata_map["test.filter"] = any_message;

  // Push dummy value at index 1, then filter name at index 2 (function expects index 2)
  lua_pushnil(lua_state_);
  lua_pushstring(lua_state_, "test.filter");

  int result = ProtobufConverterUtils::processDynamicTypedMetadataFromLuaCall(lua_state_,
                                                                              typed_metadata_map);

  EXPECT_EQ(result, 1);
  EXPECT_TRUE(lua_isnil(lua_state_, -1));
  lua_pop(lua_state_, 3); // Pop result + the 2 values we pushed
}

} // namespace
} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
