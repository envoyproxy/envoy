#include <string>
#include <vector>

#include "source/common/json/json_loader.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

using ::Envoy::StatusHelpers::StatusIs;

class JsonLoaderTest : public testing::Test {
protected:
  JsonLoaderTest() : api_(Api::createApiForTest()) {}

  ValueType getValidValue(const Json::Object& json, const std::string& key) {
    return getValid(json.getValue(key));
  }

  void expectErrorValue(const Json::Object& json, const std::string& key,
                        absl::StatusCode status_code, const std::string& message) {
    expectError(json.getValue(key), status_code, message);
  }

  ObjectSharedPtr getValidObject(const Json::Object& json, const std::string& key) {
    return getValid(json.getObjectNoThrow(key));
  }

  void expectErrorObject(const Json::Object& json, const std::string& key,
                         absl::StatusCode status_code, const std::string& message) {
    expectError(json.getObjectNoThrow(key), status_code, message);
  }

  ObjectSharedPtr loadValidJson(const std::string& json) {
    return getValid(Factory::loadFromStringNoThrow(json));
  }

  void loadInvalidJson(const std::string& json, absl::StatusCode status_code,
                       const std::string& message) {
    expectError(Factory::loadFromStringNoThrow(json), status_code, message);
  }

  template <typename Type> Type getValid(const absl::StatusOr<Type>& status_or) {
    EXPECT_TRUE(status_or.ok());
    return status_or.value();
  }

  template <typename StatusOrType>
  void expectError(const StatusOrType& status_or, absl::StatusCode status_code,
                   const std::string& message) {
    EXPECT_FALSE(status_or.ok());
    EXPECT_THAT(status_or, StatusIs(status_code));
    EXPECT_EQ(status_or.status().message(), message);
  }
  Api::ApiPtr api_;
};

TEST_F(JsonLoaderTest, Basic) {
  EXPECT_THROW(Factory::loadFromString("{"), Exception);

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\":123}");
    EXPECT_TRUE(json->hasObject("hello"));
    EXPECT_FALSE(json->hasObject("world"));
    EXPECT_FALSE(json->empty());
    EXPECT_THROW(json->getObject("world"), Exception);
    expectErrorObject(*json, "world", absl::StatusCode::kNotFound,
                      "key 'world' missing from lines 1-1");
    EXPECT_THROW(json->getObject("hello"), Exception);
    expectErrorObject(*json, "hello", absl::StatusCode::kInternal,
                      "key 'hello' not an object from line 1");
    EXPECT_THROW(json->getBoolean("hello"), Exception);
    EXPECT_THROW(json->getObjectArray("hello"), Exception);
    EXPECT_THROW(json->getString("hello"), Exception);

    EXPECT_THROW_WITH_MESSAGE(json->getString("hello"), Exception,
                              "key 'hello' missing or not a string from lines 1-1");
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\":\"123\"\n}");
    EXPECT_THROW_WITH_MESSAGE(json->getInteger("hello"), Exception,
                              "key 'hello' missing or not an integer from lines 1-2");
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\":true}");
    EXPECT_TRUE(json->getBoolean("hello"));
    EXPECT_TRUE(json->getBoolean("hello", false));
    EXPECT_FALSE(json->getBoolean("world", false));

    EXPECT_TRUE(absl::get<bool>(getValidValue(*json, "hello")));
    expectErrorValue(*json, "world", absl::StatusCode::kNotFound,
                     "key 'world' missing from lines 1-1");
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\": [\"a\", \"b\", 3]}");
    EXPECT_THROW(json->getStringArray("hello"), Exception);
    EXPECT_THROW(json->getStringArray("world"), Exception);
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\":123}");
    EXPECT_EQ(123, json->getInteger("hello", 456));
    EXPECT_EQ(456, json->getInteger("world", 456));

    EXPECT_EQ(123, absl::get<int64_t>(getValidValue(*json, "hello")));
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\": \n[123]}");

    EXPECT_THROW_WITH_MESSAGE(
        json->getObjectArray("hello").at(0)->getString("hello"), Exception,
        "JSON field from line 2 accessed with type 'Object' does not match actual type 'Integer'.");
  }

  {
    EXPECT_THROW_WITH_MESSAGE(Factory::loadFromString("{\"hello\": \n\n\"world\""), Exception,
                              "JSON supplied is not valid. Error(line 3, column 8, token "
                              "\"world\"): syntax error while "
                              "parsing object - unexpected end of input; expected '}'\n");
  }

  {
    loadInvalidJson("{\"hello\": \n\n\"world\"", absl::StatusCode::kInternal,
                    "JSON supplied is not valid. Error(line 3, column 8, token "
                    "\"world\"): syntax error while "
                    "parsing object - unexpected end of input; expected '}'\n");
  }

  {
    ObjectSharedPtr json_object = Factory::loadFromString("[\"foo\",\"bar\"]");
    EXPECT_FALSE(json_object->empty());
  }

  {
    ObjectSharedPtr json_object = Factory::loadFromString("[]");
    EXPECT_TRUE(json_object->empty());
  }

  {
    ObjectSharedPtr json =
        Factory::loadFromString("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
    int pos = 0;
    json->iterate([this, &pos](const std::string& key, const Json::Object& value) {
      EXPECT_TRUE(key == "1" || key == "2");

      if (key == "1") {
        EXPECT_EQ("111", value.getString("11"));
        EXPECT_EQ("111", absl::get<std::string>(getValidValue(value, "11")));
      } else {
        EXPECT_EQ("222", value.getString("22"));
        EXPECT_EQ("222", absl::get<std::string>(getValidValue(value, "22")));
      }

      pos++;
      return true;
    });

    EXPECT_EQ(2, pos);
  }

  {
    ObjectSharedPtr json =
        Factory::loadFromString("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
    int pos = 0;
    json->iterate([&pos](const std::string& key, const Json::Object& value) {
      EXPECT_TRUE(key == "1" || key == "2");

      if (key == "1") {
        EXPECT_EQ("111", value.getString("11"));
      } else {
        EXPECT_EQ("222", value.getString("22"));
      }

      pos++;
      return false;
    });

    EXPECT_EQ(1, pos);
  }

  {
    std::string json = R"EOF(
    {
      "descriptors": [
         [{"key": "hello", "value": "world"}, {"key": "foo", "value": "bar"}],
         [{"key": "foo2", "value": "bar2"}]
       ]
    }
    )EOF";

    ObjectSharedPtr config = Factory::loadFromString(json);
    EXPECT_EQ(2U, config->getObjectArray("descriptors")[0]->asObjectArray().size());
    EXPECT_EQ(1U, config->getObjectArray("descriptors")[1]->asObjectArray().size());
  }

  {
    std::string json = R"EOF(
    {
      "descriptors": ["hello", "world"]
    }
    )EOF";

    ObjectSharedPtr config = Factory::loadFromString(json);
    std::vector<ObjectSharedPtr> array = config->getObjectArray("descriptors");
    EXPECT_THROW(array[0]->asObjectArray(), Exception);

    // Object Array is not supported as an value.
    expectErrorValue(*config, "descriptors", absl::StatusCode::kInternal,
                     "key 'descriptors' not a value type from lines 2-4");
  }

  {
    std::string json = R"EOF({})EOF";
    ObjectSharedPtr config = Factory::loadFromString(json);
    ObjectSharedPtr object = config->getObject("foo", true);
    EXPECT_EQ(2, object->getInteger("bar", 2));
    EXPECT_TRUE(object->empty());
  }

  {
    std::string json = R"EOF({"foo": []})EOF";
    ObjectSharedPtr config = Factory::loadFromString(json);
    EXPECT_TRUE(config->getStringArray("foo").empty());
  }

  {
    std::string json = R"EOF({"foo": ["bar", "baz"]})EOF";
    ObjectSharedPtr config = Factory::loadFromString(json);
    EXPECT_FALSE(config->getStringArray("foo").empty());
  }

  {
    std::string json = R"EOF({})EOF";
    ObjectSharedPtr config = Factory::loadFromString(json);
    EXPECT_THROW(config->getStringArray("foo"), EnvoyException);
  }

  {
    std::string json = R"EOF({})EOF";
    ObjectSharedPtr config = Factory::loadFromString(json);
    EXPECT_TRUE(config->getStringArray("foo", true).empty());
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\": \n[2.0]}");
    EXPECT_THROW(json->getObjectArray("hello").at(0)->getDouble("foo"), Exception);
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\": \n[null]}");
    EXPECT_THROW(json->getObjectArray("hello").at(0)->getDouble("foo"), Exception);
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{}");
    EXPECT_THROW((void)json->getObjectArray("hello").empty(), Exception);
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{}");
    EXPECT_TRUE(json->getObjectArray("hello", true).empty());
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("[ null ]");
    EXPECT_EQ(json->asJsonString(), "[null]");
  }

  {
    ObjectSharedPtr json1 = Factory::loadFromString("[ [ ] , { } ]");
    EXPECT_EQ(json1->asJsonString(), "[null,null]");
  }

  {
    ObjectSharedPtr json1 = Factory::loadFromString("[ true ]");
    EXPECT_EQ(json1->asJsonString(), "[true]");
  }

  {
    ObjectSharedPtr json1 = Factory::loadFromString("{\"foo\": 123, \"bar\": \"cat\"}");
    EXPECT_EQ(json1->asJsonString(), "{\"bar\":\"cat\",\"foo\":123}");
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\": {}}");
    EXPECT_EQ(json->getObject("hello")->asJsonString(), "null");
  }

  {
    ObjectSharedPtr json = loadValidJson("{\"hello\": {}}");
    EXPECT_EQ(getValidObject(*json, "hello")->asJsonString(), "null");
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\": [] }");
    EXPECT_EQ(json->asJsonString(), "{\"hello\":null}");
  }
}

TEST_F(JsonLoaderTest, Integer) {
  {
    ObjectSharedPtr json =
        Factory::loadFromString("{\"max\":9223372036854775807, \"min\":-9223372036854775808}");
    EXPECT_EQ(std::numeric_limits<int64_t>::max(), json->getInteger("max"));
    EXPECT_EQ(std::numeric_limits<int64_t>::min(), json->getInteger("min"));
  }
  {
    EXPECT_THROW(Factory::loadFromString("{\"val\":9223372036854775808}"), EnvoyException);
    ObjectSharedPtr json = Factory::loadFromString("{\"val\":-9223372036854775809}");
    EXPECT_THROW(json->getInteger("val"), EnvoyException);
  }
  // Number overflow exception.
  {
    EXPECT_THROW(Factory::loadFromString("-"
                                         "521111111111111111111111111111111111111111111111111111111"
                                         "111111111111111111111111111111111111111111111111111111111"
                                         "111111111111111111111111111111111111111111111111111111111"
                                         "111111111111111111111111111111111111111111111111111111111"
                                         "111111111111111111111111111111111111111111111111111111111"
                                         "111111111111111111111111111111111111111111111111111111111"
                                         "1111111111111111111111111111111111111111111111111"),
                 EnvoyException);
  }
}

TEST_F(JsonLoaderTest, Double) {
  {
    ObjectSharedPtr json = Factory::loadFromString("{\"value1\": 10.5, \"value2\": -12.3}");
    EXPECT_EQ(10.5, json->getDouble("value1"));
    EXPECT_EQ(-12.3, json->getDouble("value2"));

    EXPECT_EQ(10.5, absl::get<double>(getValidValue(*json, "value1")));
    EXPECT_EQ(-12.3, absl::get<double>(getValidValue(*json, "value2")));
  }
  {
    ObjectSharedPtr json = Factory::loadFromString("{\"foo\": 13.22}");
    EXPECT_EQ(13.22, json->getDouble("foo", 0));
    EXPECT_EQ(0, json->getDouble("bar", 0));

    EXPECT_EQ(13.22, absl::get<double>(getValidValue(*json, "foo")));
  }
  {
    ObjectSharedPtr json = Factory::loadFromString("{\"foo\": \"bar\"}");
    EXPECT_THROW(json->getDouble("foo"), Exception);
  }
}

TEST_F(JsonLoaderTest, LoadArray) {
  ObjectSharedPtr json1 = Factory::loadFromString("[1.11, 22, \"cat\"]");
  ObjectSharedPtr json2 = Factory::loadFromString("[22, \"cat\", 1.11]");

  // Array values in different orders will not be the same.
  EXPECT_NE(json1->asJsonString(), json2->asJsonString());
  EXPECT_NE(json1->hash(), json2->hash());
}

TEST_F(JsonLoaderTest, Hash) {
  ObjectSharedPtr json1 = Factory::loadFromString("{\"value1\": 10.5, \"value2\": -12.3}");
  ObjectSharedPtr json2 = Factory::loadFromString("{\"value2\": -12.3, \"value1\": 10.5}");
  ObjectSharedPtr json3 = Factory::loadFromString("  {  \"value2\":  -12.3, \"value1\":  10.5} ");
  ObjectSharedPtr json4 = Factory::loadFromString("{\"value1\": 10.5}");

  // Objects with keys in different orders should be the same
  EXPECT_EQ(json1->hash(), json2->hash());
  // Whitespace is ignored
  EXPECT_EQ(json2->hash(), json3->hash());
  // Ensure different hash is computed for different objects
  EXPECT_NE(json1->hash(), json4->hash());

  // Nested objects with keys in different orders should be the same.
  ObjectSharedPtr json5 = Factory::loadFromString("{\"value1\": {\"a\": true, \"b\": null}}");
  ObjectSharedPtr json6 = Factory::loadFromString("{\"value1\": {\"b\": null, \"a\": true}}");
  EXPECT_EQ(json5->hash(), json6->hash());
}

TEST_F(JsonLoaderTest, Schema) {
  std::string invalid_schema = R"EOF(
    {
      "properties" : {
        "value1": {"type" : "faketype"}
      }
    }
    )EOF";

  std::string json_string = R"EOF(
    {
      "value1": 10,
      "value2" : "test"
    }
    )EOF";

  ObjectSharedPtr json = Factory::loadFromString(json_string);
  EXPECT_THROW_WITH_MESSAGE(json->validateSchema(invalid_schema), Exception, "not implemented");
}

TEST_F(JsonLoaderTest, MissingEnclosingDocument) {

  std::string json_string = R"EOF(
  "listeners" : [
    {
      "address": "tcp://127.0.0.1:1234",
      "filters": []
    }
  ]
  )EOF";
  EXPECT_THROW_WITH_MESSAGE(
      Factory::loadFromString(json_string), Exception,
      "JSON supplied is not valid. Error(line 2, column 15, token \"listeners\" :): syntax error "
      "while parsing value - unexpected ':'; expected end of input\n");
}

TEST_F(JsonLoaderTest, AsString) {
  ObjectSharedPtr json = Factory::loadFromString("{\"name1\": \"value1\", \"name2\": true}");
  json->iterate([&](const std::string& key, const Json::Object& value) {
    EXPECT_TRUE(key == "name1" || key == "name2");

    if (key == "name1") {
      EXPECT_EQ("value1", value.asString());
    } else {
      EXPECT_THROW(value.asString(), Exception);
    }
    return true;
  });
}

TEST_F(JsonLoaderTest, LoadFromStruct) {
  const std::string json_string = R"EOF({
    "struct": {
      "struct_string": "plain_string_value",
      "struct_protocol": "HTTP/1.1",
      "struct_struct": {
        "struct_struct_string": "plain_string_value",
        "struct_struct_protocol": "HTTP/1.1",
        "struct_struct_struct": {
          "struct_struct_struct_string": "plain_string_value",
          "struct_struct_struct_protocol": "HTTP/1.1",
          "struct_struct_number": 53,
          "struct_struct_null": null,
          "struct_struct_bool": true,
        },
        "struct_struct_list": [
          "struct_struct_list_string",
          "HTTP/1.1",
        ],
      },
      "struct_list": [
        "struct_list_string",
        "HTTP/1.1",
        {
          "struct_list_struct_string": "plain_string_value",
          "struct_list_struct_protocol": "HTTP/1.1",
        },
        [
          "struct_list_list_string",
          "HTTP/1.1",
        ],
      ],
    },
    "list": [
      "list_string",
      "HTTP/1.1",
      {
        "list_struct_string": "plain_string_value",
        "list_struct_protocol": "HTTP/1.1",
        "list_struct_struct": {
          "list_struct_struct_string": "plain_string_value",
          "list_struct_struct_protocol": "HTTP/1.1",
        },
        "list_struct_list": [
          "list_struct_list_string",
          "HTTP/1.1",
        ]
      },
      [
        "list_list_string",
        "HTTP/1.1",
        {
          "list_list_struct_string": "plain_string_value",
          "list_list_struct_protocol": "HTTP/1.1",
        },
        [
          "list_list_list_string",
          "HTTP/1.1",
        ],
      ],
    ],
  })EOF";

  const ProtobufWkt::Struct src = TestUtility::jsonToStruct(json_string);
  ObjectSharedPtr json = Factory::loadFromProtobufStruct(src);
  const auto output_json = json->asJsonString();
  EXPECT_TRUE(TestUtility::jsonStringEqual(output_json, json_string));
}

TEST_F(JsonLoaderTest, LoadFromStructUnknownValueCase) {
  ProtobufWkt::Struct src;
  ProtobufWkt::Value value_not_set;
  (*src.mutable_fields())["field"] = value_not_set;
  EXPECT_THROW_WITH_MESSAGE(Factory::loadFromProtobufStruct(src), Exception,
                            "Protobuf value case not implemented");
}

} // namespace
} // namespace Json
} // namespace Envoy
