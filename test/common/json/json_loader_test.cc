#include "common/json/json_loader.h"

namespace Json {

TEST(JsonLoaderTest, Basic) {
  EXPECT_THROW(FileLoader("bad_file"), Exception);
  EXPECT_THROW(StringLoader("{"), Exception);

  {
    StringLoader json("{\"hello\":123}");
    EXPECT_TRUE(json.hasObject("hello"));
    EXPECT_FALSE(json.hasObject("world"));
    EXPECT_THROW(json.getObject("world"), Exception);
    EXPECT_THROW(json.getBoolean("hello"), Exception);
    EXPECT_THROW(json.getObjectArray("hello"), Exception);
    EXPECT_THROW(json.getString("hello"), Exception);
  }

  {
    StringLoader json("{\"hello\":\"123\"}");
    EXPECT_THROW(json.getInteger("hello"), Exception);
  }

  {
    StringLoader json("{\"hello\":true}");
    EXPECT_TRUE(json.getBoolean("hello"));
    EXPECT_TRUE(json.getBoolean("hello", false));
    EXPECT_FALSE(json.getBoolean("world", false));
  }

  {
    StringLoader json("{\"hello\": [\"a\", \"b\", 3]}");
    EXPECT_THROW(json.getStringArray("hello"), Exception);
  }

  {
    StringLoader json("{\"hello\":123}");
    EXPECT_EQ(123, json.getInteger("hello", 456));
    EXPECT_EQ(456, json.getInteger("world", 456));
  }

  {
    StringLoader json("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
    int pos = 0;
    json.iterate([&pos](const std::string& key, const Json::Object& value) {
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
    StringLoader json("{\"1\":{\"11\":\"111\"},\"2\":{\"22\":\"222\"}}");
    int pos = 0;
    json.iterate([&pos](const std::string& key, const Json::Object& value) {
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

    Json::StringLoader config(json);
    EXPECT_EQ(2U, config.getObjectArray("descriptors")[0].asObjectArray().size());
    EXPECT_EQ(1U, config.getObjectArray("descriptors")[1].asObjectArray().size());
  }

  {
    std::string json = R"EOF(
    {
      "descriptors": ["hello", "world"]
    }
    )EOF";

    Json::StringLoader config(json);
    std::vector<Object> array = config.getObjectArray("descriptors");
    EXPECT_THROW(array[0].asObjectArray(), Exception);
  }

  {
    std::string json = R"EOF(
    {
    }
    )EOF";

    Json::StringLoader config(json);
    Object object = config.getObject("foo", true);
    EXPECT_EQ(2, object.getInteger("bar", 2));
  }
}

TEST(JsonLoaderTest, Integer) {
  {
    StringLoader json("{\"max\":9223372036854775807, \"min\":-9223372036854775808}");
    EXPECT_EQ(std::numeric_limits<int64_t>::max(), json.getInteger("max"));
    EXPECT_EQ(std::numeric_limits<int64_t>::min(), json.getInteger("min"));
  }
}

TEST(JsonLoaderTest, Double) {
  {
    StringLoader json("{\"value1\": 10.5, \"value2\": -12.3}");
    EXPECT_EQ(10.5, json.getDouble("value1"));
    EXPECT_EQ(-12.3, json.getDouble("value2"));
  }
  {
    StringLoader json("{\"foo\": 13.22}");
    EXPECT_EQ(13.22, json.getDouble("foo", 0));
    EXPECT_EQ(0, json.getDouble("bar", 0));
  }
  {
    StringLoader json("{\"foo\": \"bar\"}");
    EXPECT_THROW(json.getDouble("foo"), Exception);
  }
}

} // Json
