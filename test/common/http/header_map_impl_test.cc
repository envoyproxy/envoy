#include <string>

#include "common/http/header_map_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

TEST(HeaderStringTest, All) {
  // Static LowerCaseString constructor
  {
    LowerCaseString static_string("hello");
    HeaderString string(static_string);
    EXPECT_STREQ("hello", string.c_str());
    EXPECT_EQ(static_string.get().c_str(), string.c_str());
    EXPECT_EQ(5U, string.size());
  }

  // Static std::string constructor
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_STREQ("HELLO", string.c_str());
    EXPECT_EQ(static_string.c_str(), string.c_str());
    EXPECT_EQ(5U, string.size());
  }

  // Static move contructor
  {
    std::string static_string("HELLO");
    HeaderString string1(static_string);
    HeaderString string2(std::move(string1));
    EXPECT_STREQ("HELLO", string2.c_str());
    EXPECT_EQ(static_string.c_str(), string1.c_str());
    EXPECT_EQ(static_string.c_str(), string2.c_str());
    EXPECT_EQ(5U, string1.size());
    EXPECT_EQ(5U, string2.size());
  }

  // Inline move constructor
  {
    HeaderString string;
    string.setCopy("hello", 5);
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    HeaderString string2(std::move(string));
    EXPECT_TRUE(string.empty());
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_EQ(HeaderString::Type::Inline, string2.type());
    string.append("world", 5);
    EXPECT_STREQ("world", string.c_str());
    EXPECT_EQ(5UL, string.size());
    EXPECT_STREQ("hello", string2.c_str());
    EXPECT_EQ(5UL, string2.size());
  }

  // Dynamic move constructor
  {
    std::string large(4096, 'a');
    HeaderString string;
    string.setCopy(large.c_str(), large.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    HeaderString string2(std::move(string));
    EXPECT_TRUE(string.empty());
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_EQ(HeaderString::Type::Dynamic, string2.type());
    string.append("b", 1);
    EXPECT_STREQ("b", string.c_str());
    EXPECT_EQ(1UL, string.size());
    EXPECT_STREQ(large.c_str(), string2.c_str());
    EXPECT_EQ(4096UL, string2.size());
  }

  // Static to inline number.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    string.setInteger(5);
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_STREQ("5", string.c_str());
  }

  // Static to inline string.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    string.setCopy(static_string.c_str(), static_string.size());
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_STREQ("HELLO", string.c_str());
  }

  // Static clear() does nothing.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
    string.clear();
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
    EXPECT_STREQ("HELLO", string.c_str());
  }

  // Static to append.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
    string.append("a", 1);
    EXPECT_STREQ("a", string.c_str());
  }

  // Copy inline
  {
    HeaderString string;
    string.setCopy("hello", 5);
    EXPECT_STREQ("hello", string.c_str());
    EXPECT_EQ(5U, string.size());
  }

  // Copy dynamic
  {
    HeaderString string;
    std::string large_value(4096, 'a');
    string.setCopy(large_value.c_str(), large_value.size());
    EXPECT_STREQ(large_value.c_str(), string.c_str());
    EXPECT_NE(large_value.c_str(), string.c_str());
    EXPECT_EQ(4096U, string.size());
  }

  // Copy twice dynamic
  {
    HeaderString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1.c_str(), large_value1.size());
    std::string large_value2(2048, 'b');
    string.setCopy(large_value2.c_str(), large_value2.size());
    EXPECT_STREQ(large_value2.c_str(), string.c_str());
    EXPECT_NE(large_value2.c_str(), string.c_str());
    EXPECT_EQ(2048U, string.size());
  }

  // Copy twice dynamic with reallocate
  {
    HeaderString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1.c_str(), large_value1.size());
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2.c_str(), large_value2.size());
    EXPECT_STREQ(large_value2.c_str(), string.c_str());
    EXPECT_NE(large_value2.c_str(), string.c_str());
    EXPECT_EQ(16384U, string.size());
  }

  // Copy twice inline to dynamic
  {
    HeaderString string;
    std::string large_value1(16, 'a');
    string.setCopy(large_value1.c_str(), large_value1.size());
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2.c_str(), large_value2.size());
    EXPECT_STREQ(large_value2.c_str(), string.c_str());
    EXPECT_NE(large_value2.c_str(), string.c_str());
    EXPECT_EQ(16384U, string.size());
  }

  // Append, small buffer to dynamic
  {
    HeaderString string;
    std::string test(127, 'a');
    string.append(test.c_str(), test.size());
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    string.append("a", 1);
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    test += 'a';
    EXPECT_STREQ(test.c_str(), string.c_str());
  }

  // Append into inline twice, then shift to dynamic.
  {
    HeaderString string;
    string.append("hello", 5);
    EXPECT_STREQ("hello", string.c_str());
    EXPECT_EQ(5U, string.size());
    string.append("world", 5);
    EXPECT_STREQ("helloworld", string.c_str());
    EXPECT_EQ(10U, string.size());
    std::string large(4096, 'a');
    string.append(large.c_str(), large.size());
    large = "helloworld" + large;
    EXPECT_STREQ(large.c_str(), string.c_str());
    EXPECT_EQ(4106U, string.size());
  }

  // Append, realloc dynamic.
  {
    HeaderString string;
    std::string large(128, 'a');
    string.append(large.c_str(), large.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    std::string large2 = large + large;
    string.append(large2.c_str(), large2.size());
    large += large2;
    EXPECT_STREQ(large.c_str(), string.c_str());
    EXPECT_EQ(384U, string.size());
  }

  // Append, realloc close to limit with small buffer.
  {
    HeaderString string;
    std::string large(128, 'a');
    string.append(large.c_str(), large.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    std::string large2(120, 'b');
    string.append(large2.c_str(), large2.size());
    std::string large3(32, 'c');
    string.append(large3.c_str(), large3.size());
    EXPECT_STREQ((large + large2 + large3).c_str(), string.c_str());
    EXPECT_EQ(280U, string.size());
  }

  // Set integer, inline
  {
    HeaderString string;
    string.setInteger(123456789);
    EXPECT_STREQ("123456789", string.c_str());
    EXPECT_EQ(9U, string.size());
  }

  // Set integer, dynamic
  {
    HeaderString string;
    std::string large(128, 'a');
    string.append(large.c_str(), large.size());
    string.setInteger(123456789);
    EXPECT_STREQ("123456789", string.c_str());
    EXPECT_EQ(9U, string.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
  }

  // Set static, switch to dynamic, back to static.
  {
    const std::string static_string = "hello world";
    HeaderString string;
    string.setReference(static_string);
    EXPECT_EQ(string.c_str(), static_string.c_str());
    EXPECT_EQ(11U, string.size());
    EXPECT_EQ(HeaderString::Type::Reference, string.type());

    const std::string large(128, 'a');
    string.setCopy(large.c_str(), large.size());
    EXPECT_NE(string.c_str(), large.c_str());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());

    string.setReference(static_string);
    EXPECT_EQ(string.c_str(), static_string.c_str());
    EXPECT_EQ(11U, string.size());
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
  }

  // caseInsensitiveContains
  {
    const std::string static_string("keep-alive, Upgrade, close");
    HeaderString string(static_string);
    EXPECT_TRUE(string.caseInsensitiveContains("keep-alive"));
    EXPECT_TRUE(string.caseInsensitiveContains("Keep-alive"));
    EXPECT_TRUE(string.caseInsensitiveContains("Upgrade"));
    EXPECT_TRUE(string.caseInsensitiveContains("upgrade"));
    EXPECT_TRUE(string.caseInsensitiveContains("close"));
    EXPECT_TRUE(string.caseInsensitiveContains("Close"));
    EXPECT_FALSE(string.caseInsensitiveContains(""));
    EXPECT_FALSE(string.caseInsensitiveContains("keep"));
    EXPECT_FALSE(string.caseInsensitiveContains("alive"));
    EXPECT_FALSE(string.caseInsensitiveContains("grade"));

    const std::string small("close");
    string.setCopy(small.c_str(), small.size());
    EXPECT_FALSE(string.caseInsensitiveContains("keep-alive"));

    const std::string empty("");
    string.setCopy(empty.c_str(), empty.size());
    EXPECT_FALSE(string.caseInsensitiveContains("keep-alive"));
    EXPECT_FALSE(string.caseInsensitiveContains(""));
  }

  // getString
  {
    std::string static_string("HELLO");
    HeaderString headerString1(static_string);
    absl::string_view retString1 = headerString1.getStringView();
    EXPECT_EQ("HELLO", retString1);
    EXPECT_EQ(5U, retString1.size());

    HeaderString headerString2;
    absl::string_view retString2 = headerString2.getStringView();
    EXPECT_EQ(0U, retString2.size());
  }
}

TEST(HeaderMapImplTest, InlineInsert) {
  HeaderMapImpl headers;
  EXPECT_EQ(nullptr, headers.Host());
  headers.insertHost().value(std::string("hello"));
  EXPECT_STREQ(":authority", headers.Host()->key().c_str());
  EXPECT_STREQ("hello", headers.Host()->value().c_str());
  EXPECT_STREQ("hello", headers.get(Headers::get().Host)->value().c_str());
}

TEST(HeaderMapImplTest, MoveIntoInline) {
  HeaderMapImpl headers;
  HeaderString key;
  key.setCopy(Headers::get().Host.get().c_str(), Headers::get().Host.get().size());
  HeaderString value;
  value.setCopy("hello", 5);
  headers.addViaMove(std::move(key), std::move(value));
  EXPECT_STREQ(":authority", headers.Host()->key().c_str());
  EXPECT_STREQ("hello", headers.Host()->value().c_str());
}

TEST(HeaderMapImplTest, Remove) {
  HeaderMapImpl headers;

  // Add random header and then remove by name.
  LowerCaseString static_key("hello");
  std::string ref_value("value");
  headers.addReference(static_key, ref_value);
  EXPECT_STREQ("value", headers.get(static_key)->value().c_str());
  EXPECT_EQ(HeaderString::Type::Reference, headers.get(static_key)->value().type());
  EXPECT_EQ(1UL, headers.size());
  headers.remove(static_key);
  EXPECT_EQ(nullptr, headers.get(static_key));
  EXPECT_EQ(0UL, headers.size());

  // Add and remove by inline.
  headers.insertContentLength().value(5);
  EXPECT_STREQ("5", headers.ContentLength()->value().c_str());
  EXPECT_EQ(1UL, headers.size());
  headers.removeContentLength();
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());

  // Add inline and remove by name.
  headers.insertContentLength().value(5);
  EXPECT_STREQ("5", headers.ContentLength()->value().c_str());
  EXPECT_EQ(1UL, headers.size());
  headers.remove(Headers::get().ContentLength);
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());
}

TEST(HeaderMapImplTest, RemoveRegex) {
  // These will match.
  LowerCaseString key1 = LowerCaseString("X-prefix-foo");
  LowerCaseString key3 = LowerCaseString("X-Prefix-");
  LowerCaseString key5 = LowerCaseString("x-prefix-eep");
  // These will not.
  LowerCaseString key2 = LowerCaseString(" x-prefix-foo");
  LowerCaseString key4 = LowerCaseString("y-x-prefix-foo");

  HeaderMapImpl headers;
  headers.addReference(key1, "value");
  headers.addReference(key2, "value");
  headers.addReference(key3, "value");
  headers.addReference(key4, "value");
  headers.addReference(key5, "value");

  // Test removing the first header, middle headers, and the end header.
  headers.removePrefix(LowerCaseString("x-prefix-"));
  EXPECT_EQ(nullptr, headers.get(key1));
  EXPECT_NE(nullptr, headers.get(key2));
  EXPECT_EQ(nullptr, headers.get(key3));
  EXPECT_NE(nullptr, headers.get(key4));
  EXPECT_EQ(nullptr, headers.get(key5));

  // Remove all headers.
  headers.removePrefix(LowerCaseString(""));
  EXPECT_EQ(nullptr, headers.get(key2));
  EXPECT_EQ(nullptr, headers.get(key4));

  // Add inline and remove by regex
  headers.insertContentLength().value(5);
  EXPECT_STREQ("5", headers.ContentLength()->value().c_str());
  EXPECT_EQ(1UL, headers.size());
  headers.removePrefix(LowerCaseString("content"));
  EXPECT_EQ(nullptr, headers.ContentLength());
}

TEST(HeaderMapImplTest, SetRemovesAllValues) {
  HeaderMapImpl headers;

  LowerCaseString key1("hello");
  LowerCaseString key2("olleh");
  std::string ref_value1("world");
  std::string ref_value2("planet");
  std::string ref_value3("globe");
  std::string ref_value4("earth");
  std::string ref_value5("blue marble");

  headers.addReference(key1, ref_value1);
  headers.addReference(key2, ref_value2);
  headers.addReference(key1, ref_value3);
  headers.addReference(key1, ref_value4);

  typedef testing::MockFunction<void(const std::string&, const std::string&)> MockCb;

  {
    MockCb cb;

    EXPECT_CALL(cb, Call("hello", "world"));
    EXPECT_CALL(cb, Call("olleh", "planet"));
    EXPECT_CALL(cb, Call("hello", "globe"));
    EXPECT_CALL(cb, Call("hello", "earth"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(header.key().c_str(), header.value().c_str());
          return HeaderMap::Iterate::Continue;
        },
        &cb);
  }

  headers.setReference(key1, ref_value5);

  {
    MockCb cb;

    EXPECT_CALL(cb, Call("hello", "blue marble"));
    EXPECT_CALL(cb, Call("olleh", "planet"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(header.key().c_str(), header.value().c_str());
          return HeaderMap::Iterate::Continue;
        },
        &cb);
  }
}

TEST(HeaderMapImplTest, DoubleInlineAdd) {
  HeaderMapImpl headers;
  headers.addReferenceKey(Headers::get().ContentLength, 5);
  headers.addReferenceKey(Headers::get().ContentLength, 6);
  EXPECT_STREQ("5", headers.ContentLength()->value().c_str());
  EXPECT_EQ(1UL, headers.size());
}

TEST(HeaderMapImplTest, DoubleInlineSet) {
  HeaderMapImpl headers;
  headers.setReferenceKey(Headers::get().ContentType, "blah");
  headers.setReferenceKey(Headers::get().ContentType, "text/html");
  EXPECT_STREQ("text/html", headers.ContentType()->value().c_str());
  EXPECT_EQ(1UL, headers.size());
}

TEST(HeaderMapImplTest, AddReferenceKey) {
  HeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.addReferenceKey(foo, "world");
  EXPECT_NE("world", headers.get(foo)->value().c_str());
  EXPECT_STREQ("world", headers.get(foo)->value().c_str());
}

TEST(HeaderMapImplTest, SetReferenceKey) {
  HeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.setReferenceKey(foo, "world");
  EXPECT_NE("world", headers.get(foo)->value().c_str());
  EXPECT_STREQ("world", headers.get(foo)->value().c_str());

  headers.setReferenceKey(foo, "monde");
  EXPECT_NE("monde", headers.get(foo)->value().c_str());
  EXPECT_STREQ("monde", headers.get(foo)->value().c_str());
}

TEST(HeaderMapImplTest, AddCopy) {
  HeaderMapImpl headers;

  // Start with a string value.
  std::unique_ptr<LowerCaseString> lcKeyPtr(new LowerCaseString("hello"));
  headers.addCopy(*lcKeyPtr, "world");

  const HeaderString& value = headers.get(*lcKeyPtr)->value();

  EXPECT_STREQ("world", value.c_str());
  EXPECT_EQ(5UL, value.size());

  lcKeyPtr.reset();

  const HeaderString& value2 = headers.get(LowerCaseString("hello"))->value();

  EXPECT_STREQ("world", value2.c_str());
  EXPECT_EQ(5UL, value2.size());
  EXPECT_EQ(value.c_str(), value2.c_str());
  EXPECT_EQ(1UL, headers.size());

  // Repeat with an int value.
  //
  // addReferenceKey and addCopy can both add multiple instances of a
  // given header, so we need to delete the old "hello" header.
  headers.remove(LowerCaseString("hello"));

  // Build "hello" with string concatenation to make it unlikely that the
  // compiler is just reusing the same string constant for everything.
  lcKeyPtr.reset(new LowerCaseString(std::string("he") + "llo"));
  EXPECT_STREQ("hello", lcKeyPtr->get().c_str());

  headers.addCopy(*lcKeyPtr, 42);

  const HeaderString& value3 = headers.get(*lcKeyPtr)->value();

  EXPECT_STREQ("42", value3.c_str());
  EXPECT_EQ(2UL, value3.size());

  lcKeyPtr.reset();

  const HeaderString& value4 = headers.get(LowerCaseString("hello"))->value();

  EXPECT_STREQ("42", value4.c_str());
  EXPECT_EQ(2UL, value4.size());
  EXPECT_EQ(1UL, headers.size());

  // Here, again, we'll build yet another key string.
  LowerCaseString lcKey3(std::string("he") + "ll" + "o");
  EXPECT_STREQ("hello", lcKey3.get().c_str());

  EXPECT_STREQ("42", headers.get(lcKey3)->value().c_str());
  EXPECT_EQ(2UL, headers.get(lcKey3)->value().size());
}

TEST(HeaderMapImplTest, Equality) {
  TestHeaderMapImpl headers1;
  TestHeaderMapImpl headers2;
  EXPECT_EQ(headers1, headers2);

  headers1.addCopy("hello", "world");
  EXPECT_FALSE(headers1 == headers2);

  headers2.addCopy("foo", "bar");
  EXPECT_FALSE(headers1 == headers2);
}

TEST(HeaderMapImplTest, LargeCharInHeader) {
  HeaderMapImpl headers;
  LowerCaseString static_key("\x90hello");
  std::string ref_value("value");
  headers.addReference(static_key, ref_value);
  EXPECT_STREQ("value", headers.get(static_key)->value().c_str());
}

TEST(HeaderMapImplTest, Iterate) {
  TestHeaderMapImpl headers;
  headers.addCopy("hello", "world");
  headers.addCopy("foo", "xxx");
  headers.addCopy("world", "hello");
  LowerCaseString foo_key("foo");
  headers.setReferenceKey(foo_key, "bar"); // set moves key to end

  typedef testing::MockFunction<void(const std::string&, const std::string&)> MockCb;
  MockCb cb;

  EXPECT_CALL(cb, Call("hello", "world"));
  EXPECT_CALL(cb, Call("world", "hello"));
  EXPECT_CALL(cb, Call("foo", "bar"));
  headers.iterate(
      [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
        static_cast<MockCb*>(cb_v)->Call(header.key().c_str(), header.value().c_str());
        return HeaderMap::Iterate::Continue;
      },
      &cb);
}

TEST(HeaderMapImplTest, IterateReverse) {
  TestHeaderMapImpl headers;
  headers.addCopy("hello", "world");
  headers.addCopy("foo", "bar");
  LowerCaseString world_key("world");
  headers.setReferenceKey(world_key, "hello");

  typedef testing::MockFunction<void(const std::string&, const std::string&)> MockCb;
  MockCb cb;

  EXPECT_CALL(cb, Call("world", "hello"));
  EXPECT_CALL(cb, Call("foo", "bar"));
  // no "hello"
  headers.iterateReverse(
      [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
        static_cast<MockCb*>(cb_v)->Call(header.key().c_str(), header.value().c_str());
        if ("foo" != std::string{header.key().c_str()}) {
          return HeaderMap::Iterate::Continue;
        } else {
          return HeaderMap::Iterate::Break;
        }
      },
      &cb);
}

TEST(HeaderMapImplTest, Lookup) {
  TestHeaderMapImpl headers;
  headers.addCopy("hello", "world");
  headers.insertContentLength().value(5);

  // Lookup is not supported for non predefined inline headers.
  {
    const HeaderEntry* entry;
    EXPECT_EQ(HeaderMap::Lookup::NotSupported, headers.lookup(LowerCaseString{"hello"}, &entry));
    EXPECT_EQ(nullptr, entry);
  }

  // Lookup returns the entry of a predefined inline header if it exists.
  {
    const HeaderEntry* entry;
    EXPECT_EQ(HeaderMap::Lookup::Found, headers.lookup(Headers::get().ContentLength, &entry));
    EXPECT_STREQ("5", entry->value().c_str());
  }

  // Lookup returns HeaderMap::Lookup::NotFound if a predefined inline header does not exist.
  {
    const HeaderEntry* entry;
    EXPECT_EQ(HeaderMap::Lookup::NotFound, headers.lookup(Headers::get().Host, &entry));
    EXPECT_EQ(nullptr, entry);
  }
}

TEST(HeaderMapImplTest, Get) {
  {
    const TestHeaderMapImpl headers{{":path", "/"}, {"hello", "world"}};
    EXPECT_STREQ("/", headers.get(LowerCaseString(":path"))->value().c_str());
    EXPECT_STREQ("world", headers.get(LowerCaseString("hello"))->value().c_str());
    EXPECT_EQ(nullptr, headers.get(LowerCaseString("foo")));
  }

  {
    TestHeaderMapImpl headers{{":path", "/"}, {"hello", "world"}};
    headers.get(LowerCaseString(":path"))->value(std::string("/new_path"));
    EXPECT_STREQ("/new_path", headers.get(LowerCaseString(":path"))->value().c_str());
    headers.get(LowerCaseString("hello"))->value(std::string("world2"));
    EXPECT_STREQ("world2", headers.get(LowerCaseString("hello"))->value().c_str());
    EXPECT_EQ(nullptr, headers.get(LowerCaseString("foo")));
  }
}

} // namespace Http
} // namespace Envoy
