#include "source/extensions/filters/common/lua/protobuf_converter.h"

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

  ProtobufConverterUtils::messageToLuaTable(lua_state_, metadata);

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

} // namespace
} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
