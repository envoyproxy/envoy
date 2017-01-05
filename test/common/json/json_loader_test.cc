#include "common/json/json_loader.h"

namespace Json {

TEST(JsonLoaderTest, Basic) {
  EXPECT_THROW(Factory::LoadFromFile("bad_file"), Exception);
  EXPECT_THROW(Factory::LoadFromString("{"), Exception);

  {
    ObjectPtr json = Factory::LoadFromString("{\"hello\":123}");
    EXPECT_TRUE(json->hasObject("hello"));
    EXPECT_FALSE(json->hasObject("world"));
    EXPECT_THROW(json->getObject("world"), Exception);
    EXPECT_THROW(json->getBoolean("hello"), Exception);
    EXPECT_THROW(json->getObjectArray("hello"), Exception);
    EXPECT_THROW(json->getString("hello"), Exception);
  }

  {
    ObjectPtr json = Factory::LoadFromString("{\"hello\":\"123\"}");
    EXPECT_THROW(json->getInteger("hello"), Exception);
  }

  {
    ObjectPtr json = Factory::LoadFromString("{\"hello\":true}");
    EXPECT_TRUE(json->getBoolean("hello"));
    EXPECT_TRUE(json->getBoolean("hello", false));
    EXPECT_FALSE(json->getBoolean("world", false));
  }

  {
    ObjectPtr json = Factory::LoadFromString("{\"hello\": [\"a\", \"b\", 3]}");
    EXPECT_THROW(json->getStringArray("hello"), Exception);
    EXPECT_THROW(json->getStringArray("world"), Exception);
  }

  {
    ObjectPtr json = Factory::LoadFromString("{\"hello\":123}");
    EXPECT_EQ(123, json->getInteger("hello", 456));
    EXPECT_EQ(456, json->getInteger("world", 456));
  }

  {
    ObjectPtr json = Factory::LoadFromString("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
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
    ObjectPtr json = Factory::LoadFromString("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
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

    ObjectPtr config = Factory::LoadFromString(json);
    EXPECT_EQ(2U, config->getObjectArray("descriptors")[0]->asObjectArray().size());
    EXPECT_EQ(1U, config->getObjectArray("descriptors")[1]->asObjectArray().size());
  }

  {
    std::string json = R"EOF(
    {
      "descriptors": ["hello", "world"]
    }
    )EOF";

    ObjectPtr config = Factory::LoadFromString(json);
    std::vector<ObjectPtr> array = config->getObjectArray("descriptors");
    EXPECT_THROW(array[0]->asObjectArray(), Exception);
  }

  {
    std::string json = R"EOF(
    {
    }
    )EOF";

    ObjectPtr config = Factory::LoadFromString(json);
    ObjectPtr object = config->getObject("foo", true);
    EXPECT_EQ(2, object->getInteger("bar", 2));
  }
}

TEST(JsonLoaderTest, Integer) {
  {
    ObjectPtr json =
        Factory::LoadFromString("{\"max\":9223372036854775807, \"min\":-9223372036854775808}");
    EXPECT_EQ(std::numeric_limits<int64_t>::max(), json->getInteger("max"));
    EXPECT_EQ(std::numeric_limits<int64_t>::min(), json->getInteger("min"));
  }
  {
    ObjectPtr json_max = Factory::LoadFromString("{\"val\":9223372036854775808}");
    EXPECT_THROW(json_max->getInteger("val"), Exception);
    ObjectPtr json_min = Factory::LoadFromString("{\"val\":-9223372036854775809}");
    EXPECT_THROW(json_min->getInteger("val"), Exception);
  }
}

TEST(JsonLoaderTest, Double) {
  {
    ObjectPtr json = Factory::LoadFromString("{\"value1\": 10.5, \"value2\": -12.3}");
    EXPECT_EQ(10.5, json->getDouble("value1"));
    EXPECT_EQ(-12.3, json->getDouble("value2"));
  }
  {
    ObjectPtr json = Factory::LoadFromString("{\"foo\": 13.22}");
    EXPECT_EQ(13.22, json->getDouble("foo", 0));
    EXPECT_EQ(0, json->getDouble("bar", 0));
  }
  {
    ObjectPtr json = Factory::LoadFromString("{\"foo\": \"bar\"}");
    EXPECT_THROW(json->getDouble("foo"), Exception);
  }
}

TEST(JsonLoaderTest, Hash) {
  ObjectPtr json1 = Factory::LoadFromString("{\"value1\": 10.5, \"value2\": -12.3}");
  ObjectPtr json2 = Factory::LoadFromString("{\"value2\": -12.3, \"value1\": 10.5}");
  ObjectPtr json3 = Factory::LoadFromString("  {  \"value2\":  -12.3, \"value1\":  10.5} ");
  EXPECT_NE(json1->hash(), json2->hash());
  EXPECT_EQ(json2->hash(), json3->hash());
}

} // Json
