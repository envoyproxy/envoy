#include <string>
#include <vector>

#include "source/common/json/json_loader.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class JsonLoaderTest : public testing::Test {
protected:
  JsonLoaderTest() : api_(Api::createApiForTest()) {}

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
    EXPECT_THROW(json->getObject("hello"), Exception);
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
  }

  {
    ObjectSharedPtr json = Factory::loadFromString("{\"hello\": \n[123]}");

    EXPECT_THROW_WITH_MESSAGE(
        json->getObjectArray("hello").at(0)->getString("hello"), Exception,
        "JSON field from line 2 accessed with type 'Object' does not match actual type 'Integer'.");
  }

  {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.remove_legacy_json")) {
      EXPECT_THROW_WITH_MESSAGE(Factory::loadFromString("{\"hello\": \n\n\"world\""), Exception,
                                "JSON supplied is not valid. Error(line 3, column 8, token "
                                "\"world\"): syntax error while "
                                "parsing object - unexpected end of input; expected '}'\n");
    } else {
      EXPECT_THROW_WITH_MESSAGE(Factory::loadFromString("{\"hello\": \n\n\"world\""), Exception,
                                "JSON supplied is not valid. Error(offset 19, line 3): Missing a "
                                "comma or '}' after an object member.\n");
    }
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
    json->iterate([&pos](const std::string& key, const Json::Object& value) {
      EXPECT_TRUE(key == "1" || key == "2");

      if (key == "1") {
        EXPECT_EQ("111", value.getString("11"));
      } else {
        EXPECT_EQ("222", value.getString("22"));
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

    // I believe this is a bug with rapidjson.
    // It silently eats numbers below min int64_t with no exception.
    // Fail when reading key instead of on parse.
    ObjectSharedPtr json = Factory::loadFromString("{\"val\":-9223372036854775809}");
    EXPECT_THROW(json->getInteger("val"), EnvoyException);
  }
}

TEST_F(JsonLoaderTest, Double) {
  {
    ObjectSharedPtr json = Factory::loadFromString("{\"value1\": 10.5, \"value2\": -12.3}");
    EXPECT_EQ(10.5, json->getDouble("value1"));
    EXPECT_EQ(-12.3, json->getDouble("value2"));
  }
  {
    ObjectSharedPtr json = Factory::loadFromString("{\"foo\": 13.22}");
    EXPECT_EQ(13.22, json->getDouble("foo", 0));
    EXPECT_EQ(0, json->getDouble("bar", 0));
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
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.remove_legacy_json")) {
    EXPECT_THROW_WITH_MESSAGE(json->validateSchema(invalid_schema), Exception, "not implemented");
  } else {
    EXPECT_THROW_WITH_MESSAGE(
        json->validateSchema(invalid_schema), Exception,
        "JSON at lines 2-5 does not conform to schema.\n Invalid schema: #/properties/value1\n "
        "Schema violation: type\n Offending document key: #/value1");
  }
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
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.remove_legacy_json")) {
    EXPECT_THROW_WITH_MESSAGE(
        Factory::loadFromString(json_string), Exception,
        "JSON supplied is not valid. Error(line 2, column 15, token \"listeners\" :): syntax error "
        "while parsing value - unexpected ':'; expected end of input\n");
  } else {
    EXPECT_THROW_WITH_MESSAGE(Factory::loadFromString(json_string), Exception,
                              "JSON supplied is not valid. Error(offset 14, line 2): Terminate "
                              "parsing due to Handler error.\n");
  }
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

} // namespace
} // namespace Json
} // namespace Envoy
