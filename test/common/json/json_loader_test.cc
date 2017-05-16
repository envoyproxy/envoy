#include <string>
#include <vector>

#include "common/json/json_loader.h"

#include "gtest/gtest.h"

namespace Json {

TEST(JsonLoaderTest, Basic) {
  EXPECT_THROW(Factory::loadFromFile("bad_file"), Exception);
  EXPECT_THROW(Factory::loadFromString("{"), Exception);

  {
    ObjectPtr json = Factory::loadFromString("{\"hello\":123}");
    EXPECT_TRUE(json->hasObject("hello"));
    EXPECT_FALSE(json->hasObject("world"));
    EXPECT_FALSE(json->empty());
    EXPECT_THROW(json->getObject("world"), Exception);
    EXPECT_THROW(json->getBoolean("hello"), Exception);
    EXPECT_THROW(json->getObjectArray("hello"), Exception);
    EXPECT_THROW(json->getString("hello"), Exception);
  }

  {
    ObjectPtr json = Factory::loadFromString("{\"hello\":\"123\"}");
    EXPECT_THROW(json->getInteger("hello"), Exception);
  }

  {
    ObjectPtr json = Factory::loadFromString("{\"hello\":true}");
    EXPECT_TRUE(json->getBoolean("hello"));
    EXPECT_TRUE(json->getBoolean("hello", false));
    EXPECT_FALSE(json->getBoolean("world", false));
  }

  {
    ObjectPtr json = Factory::loadFromString("{\"hello\": [\"a\", \"b\", 3]}");
    EXPECT_THROW(json->getStringArray("hello"), Exception);
    EXPECT_THROW(json->getStringArray("world"), Exception);
  }

  {
    ObjectPtr json = Factory::loadFromString("{\"hello\":123}");
    EXPECT_EQ(123, json->getInteger("hello", 456));
    EXPECT_EQ(456, json->getInteger("world", 456));
  }

  {
    ObjectPtr json = Factory::loadFromString("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
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
    ObjectPtr json = Factory::loadFromString("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
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

    ObjectPtr config = Factory::loadFromString(json);
    EXPECT_EQ(2U, config->getObjectArray("descriptors")[0]->asObjectArray().size());
    EXPECT_EQ(1U, config->getObjectArray("descriptors")[1]->asObjectArray().size());
  }

  {
    std::string json = R"EOF(
    {
      "descriptors": ["hello", "world"]
    }
    )EOF";

    ObjectPtr config = Factory::loadFromString(json);
    std::vector<ObjectPtr> array = config->getObjectArray("descriptors");
    EXPECT_THROW(array[0]->asObjectArray(), Exception);
  }

  {
    std::string json = R"EOF(
    {
    }
    )EOF";

    ObjectPtr config = Factory::loadFromString(json);
    ObjectPtr object = config->getObject("foo", true);
    EXPECT_EQ(2, object->getInteger("bar", 2));
    EXPECT_TRUE(object->empty());
  }
}

TEST(JsonLoaderTest, Integer) {
  {
    ObjectPtr json =
        Factory::loadFromString("{\"max\":9223372036854775807, \"min\":-9223372036854775808}");
    EXPECT_EQ(std::numeric_limits<int64_t>::max(), json->getInteger("max"));
    EXPECT_EQ(std::numeric_limits<int64_t>::min(), json->getInteger("min"));
  }
  {
    EXPECT_THROW(Factory::loadFromString("{\"val\":9223372036854775808}"), EnvoyException);

    // I believe this is a bug with rapidjson.
    // It silently eats numbers below min int64_t with no exception.
    // Fail when reading key instead of on parse.
    ObjectPtr json = Factory::loadFromString("{\"val\":-9223372036854775809}");
    EXPECT_THROW(json->getInteger("val"), EnvoyException);
  }
}

TEST(JsonLoaderTest, Double) {
  {
    ObjectPtr json = Factory::loadFromString("{\"value1\": 10.5, \"value2\": -12.3}");
    EXPECT_EQ(10.5, json->getDouble("value1"));
    EXPECT_EQ(-12.3, json->getDouble("value2"));
  }
  {
    ObjectPtr json = Factory::loadFromString("{\"foo\": 13.22}");
    EXPECT_EQ(13.22, json->getDouble("foo", 0));
    EXPECT_EQ(0, json->getDouble("bar", 0));
  }
  {
    ObjectPtr json = Factory::loadFromString("{\"foo\": \"bar\"}");
    EXPECT_THROW(json->getDouble("foo"), Exception);
  }
}

TEST(JsonLoaderTest, Hash) {
  ObjectPtr json1 = Factory::loadFromString("{\"value1\": 10.5, \"value2\": -12.3}");
  ObjectPtr json2 = Factory::loadFromString("{\"value2\": -12.3, \"value1\": 10.5}");
  ObjectPtr json3 = Factory::loadFromString("  {  \"value2\":  -12.3, \"value1\":  10.5} ");
  EXPECT_NE(json1->hash(), json2->hash());
  EXPECT_EQ(json2->hash(), json3->hash());
}

TEST(JsonLoaderTest, Schema) {
  std::string invalid_json_schema = R"EOF(
  {
    "properties": {"value1"}
  }
  )EOF";

  std::string invalid_schema = R"EOF(
  {
    "properties" : {
      "value1": {"type" : "faketype"}
    }
  }
  )EOF";

  std::string different_schema = R"EOF(
  {
    "properties" : {
      "value1" : {"type" : "number"}
    },
    "additionalProperties" : false
  }
  )EOF";

  std::string valid_schema = R"EOF(
  {
    "properties": {
      "value1": {"type" : "number"},
      "value2": {"type": "string"}
    },
    "additionalProperties": false
  }
  )EOF";

  std::string json_string = R"EOF(
  {
    "value1": 10,
    "value2" : "test"
  }
  )EOF";

  ObjectPtr json = Factory::loadFromString(json_string);

  EXPECT_THROW(json->validateSchema(invalid_json_schema), std::invalid_argument);
  EXPECT_THROW(json->validateSchema(invalid_schema), Exception);
  EXPECT_THROW(json->validateSchema(different_schema), Exception);
  EXPECT_NO_THROW(json->validateSchema(valid_schema));
}

TEST(JsonLoaderTest, AsString) {
  ObjectPtr json = Factory::loadFromString("{\"name1\": \"value1\", \"name2\": true}");
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

TEST(JsonLoaderTest, ListAsString) {
  {
    std::list<std::string> list = {};
    Json::ObjectPtr json = Json::Factory::loadFromString(Json::Factory::listAsJsonString(list));
    std::vector<Json::ObjectPtr> output = json->asObjectArray();
    EXPECT_TRUE(output.empty());
  }

  {
    std::list<std::string> list = {"one"};
    Json::ObjectPtr json = Json::Factory::loadFromString(Json::Factory::listAsJsonString(list));
    std::vector<Json::ObjectPtr> output = json->asObjectArray();
    EXPECT_EQ(1, output.size());
    EXPECT_EQ("one", output[0]->asString());
  }

  {
    std::list<std::string> list = {"one", "two", "three", "four"};
    Json::ObjectPtr json = Json::Factory::loadFromString(Json::Factory::listAsJsonString(list));
    std::vector<Json::ObjectPtr> output = json->asObjectArray();
    EXPECT_EQ(4, output.size());
    EXPECT_EQ("one", output[0]->asString());
    EXPECT_EQ("two", output[1]->asString());
    EXPECT_EQ("three", output[2]->asString());
    EXPECT_EQ("four", output[3]->asString());
  }

  {
    std::list<std::string> list = {"127.0.0.1:46465", "127.0.0.1:52211", "127.0.0.1:58941"};
    Json::ObjectPtr json = Json::Factory::loadFromString(Json::Factory::listAsJsonString(list));
    std::vector<Json::ObjectPtr> output = json->asObjectArray();
    EXPECT_EQ(3, output.size());
    EXPECT_EQ("127.0.0.1:46465", output[0]->asString());
    EXPECT_EQ("127.0.0.1:52211", output[1]->asString());
    EXPECT_EQ("127.0.0.1:58941", output[2]->asString());
  }
}

} // Json
