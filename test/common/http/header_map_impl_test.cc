#include <memory>
#include <string>

#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::InSequence;

namespace Envoy {
namespace Http {

TEST(HeaderStringTest, All) {
  // Static LowerCaseString constructor
  {
    LowerCaseString static_string("hello");
    HeaderString string(static_string);
    EXPECT_EQ("hello", string.getStringView());
    EXPECT_EQ(static_string.get(), string.getStringView());
    EXPECT_EQ(5U, string.size());
  }

  // Static LowerCaseString operators
  {
    LowerCaseString banana("banana");
    LowerCaseString lemon("lemon");
    EXPECT_TRUE(banana < lemon);
    EXPECT_TRUE(banana != lemon);
    EXPECT_TRUE(banana == banana);
  }

  // Static std::string constructor
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_EQ("HELLO", string.getStringView());
    EXPECT_EQ(static_string, string.getStringView());
    EXPECT_EQ(5U, string.size());
  }

  // Static move constructor
  {
    std::string static_string("HELLO");
    HeaderString string1(static_string);
    HeaderString string2(std::move(string1));
    EXPECT_EQ("HELLO", string2.getStringView());
    EXPECT_EQ(static_string, string1.getStringView()); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(static_string, string2.getStringView());
    EXPECT_EQ(5U, string1.size());
    EXPECT_EQ(5U, string2.size());
  }

  // Inline move constructor
  {
    HeaderString string;
    string.setCopy("hello", 5);
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    HeaderString string2(std::move(string));
    EXPECT_TRUE(string.empty()); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_EQ(HeaderString::Type::Inline, string2.type());
    string.append("world", 5);
    EXPECT_EQ("world", string.getStringView());
    EXPECT_EQ(5UL, string.size());
    EXPECT_EQ("hello", string2.getStringView());
    EXPECT_EQ(5UL, string2.size());
  }

  // Dynamic move constructor
  {
    std::string large(4096, 'a');
    HeaderString string;
    string.setCopy(large.c_str(), large.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    HeaderString string2(std::move(string));
    EXPECT_TRUE(string.empty()); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_EQ(HeaderString::Type::Dynamic, string2.type());
    string.append("b", 1);
    EXPECT_EQ("b", string.getStringView());
    EXPECT_EQ(1UL, string.size());
    EXPECT_EQ(large, string2.getStringView());
    EXPECT_EQ(4096UL, string2.size());
  }

  // Static to inline number.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    string.setInteger(5);
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_EQ("5", string.getStringView());
  }

  // Static to inline string.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    string.setCopy(static_string.c_str(), static_string.size());
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    EXPECT_EQ("HELLO", string.getStringView());
  }

  // Static clear() does nothing.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
    string.clear();
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
    EXPECT_EQ("HELLO", string.getStringView());
  }

  // Static to append.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
    string.append("a", 1);
    EXPECT_EQ("HELLOa", string.getStringView());
  }

  // Copy inline
  {
    HeaderString string;
    string.setCopy("hello", 5);
    EXPECT_EQ("hello", string.getStringView());
    EXPECT_EQ(5U, string.size());
  }

  // Copy dynamic
  {
    HeaderString string;
    std::string large_value(4096, 'a');
    string.setCopy(large_value.c_str(), large_value.size());
    EXPECT_EQ(large_value, string.getStringView());
    EXPECT_NE(large_value.c_str(), string.getStringView().data());
    EXPECT_EQ(4096U, string.size());
  }

  // Copy twice dynamic
  {
    HeaderString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1.c_str(), large_value1.size());
    std::string large_value2(2048, 'b');
    string.setCopy(large_value2.c_str(), large_value2.size());
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(2048U, string.size());
  }

  // Copy twice dynamic with reallocate
  {
    HeaderString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1.c_str(), large_value1.size());
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2.c_str(), large_value2.size());
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(16384U, string.size());
  }

  // Copy twice inline to dynamic
  {
    HeaderString string;
    std::string large_value1(16, 'a');
    string.setCopy(large_value1.c_str(), large_value1.size());
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2.c_str(), large_value2.size());
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(16384U, string.size());
  }

  // Copy, exactly filling inline capacity
  //
  // ASAN does not catch the clobber in the case where the code writes one past the
  // end of the inline buffer. To ensure coverage the next block checks that setCopy
  // is not introducing a NUL in a way that does not rely on an actual clobber getting
  // detected.
  {
    HeaderString string;
    std::string large(128, 'z');
    string.setCopy(large.c_str(), large.size());
    EXPECT_EQ(string.type(), HeaderString::Type::Inline);
    EXPECT_EQ(string.getStringView(), large);
  }

  // Ensure setCopy does not add NUL.
  {
    HeaderString string;
    std::string large(128, 'z');
    string.setCopy(large.c_str(), large.size());
    EXPECT_EQ(string.type(), HeaderString::Type::Inline);
    EXPECT_EQ(string.getStringView(), large);
    std::string small(1, 'a');
    string.setCopy(small.c_str(), small.size());
    EXPECT_EQ(string.type(), HeaderString::Type::Inline);
    EXPECT_EQ(string.getStringView(), small);
    // If we peek past the valid first character of the
    // header string_view it should still be 'z' and not '\0'.
    // We know this peek is OK since the memory is much larger
    // than two bytes.
    EXPECT_EQ(string.getStringView().data()[1], 'z');
  }

  // Copy, exactly filling dynamic capacity
  //
  // ASAN should catch a write one past the end of the dynamic buffer. This test
  // forces a dynamic buffer with one copy and then fills it with the next.
  {
    HeaderString string;
    // Force Dynamic with setCopy of inline buffer size + 1.
    std::string large1(129, 'z');
    string.setCopy(large1.c_str(), large1.size());
    EXPECT_EQ(string.type(), HeaderString::Type::Dynamic);
    const void* dynamic_buffer_address = string.getStringView().data();
    // Dynamic capacity in setCopy is 2x required by the size.
    // So to fill it exactly setCopy with a total of 258 chars.
    std::string large2(258, 'z');
    string.setCopy(large2.c_str(), large2.size());
    EXPECT_EQ(string.type(), HeaderString::Type::Dynamic);
    // The actual buffer address should be the same as it was after
    // setCopy(large1), ensuring no reallocation occurred.
    EXPECT_EQ(string.getStringView().data(), dynamic_buffer_address);
    EXPECT_EQ(string.getStringView(), large2);
  }

  // Append, small buffer to dynamic
  {
    HeaderString string;
    std::string test(128, 'a');
    string.append(test.c_str(), test.size());
    EXPECT_EQ(HeaderString::Type::Inline, string.type());
    string.append("a", 1);
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    test += 'a';
    EXPECT_EQ(test, string.getStringView());
  }

  // Append into inline twice, then shift to dynamic.
  {
    HeaderString string;
    string.append("hello", 5);
    EXPECT_EQ("hello", string.getStringView());
    EXPECT_EQ(5U, string.size());
    string.append("world", 5);
    EXPECT_EQ("helloworld", string.getStringView());
    EXPECT_EQ(10U, string.size());
    std::string large(4096, 'a');
    string.append(large.c_str(), large.size());
    large = "helloworld" + large;
    EXPECT_EQ(large, string.getStringView());
    EXPECT_EQ(4106U, string.size());
  }

  // Append, realloc dynamic.
  {
    HeaderString string;
    std::string large(129, 'a');
    string.append(large.c_str(), large.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    std::string large2 = large + large;
    string.append(large2.c_str(), large2.size());
    large += large2;
    EXPECT_EQ(large, string.getStringView());
    EXPECT_EQ(387U, string.size());
  }

  // Append, realloc close to limit with small buffer.
  {
    HeaderString string;
    std::string large(129, 'a');
    string.append(large.c_str(), large.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
    std::string large2(120, 'b');
    string.append(large2.c_str(), large2.size());
    std::string large3(32, 'c');
    string.append(large3.c_str(), large3.size());
    EXPECT_EQ((large + large2 + large3), string.getStringView());
    EXPECT_EQ(281U, string.size());
  }

  // Append, exactly filling dynamic capacity
  //
  // ASAN should catch a write one past the end of the dynamic buffer. This test
  // forces a dynamic buffer with one copy and then fills it with the next.
  {
    HeaderString string;
    // Force Dynamic with setCopy of inline buffer size + 1.
    std::string large1(129, 'z');
    string.setCopy(large1.c_str(), large1.size());
    EXPECT_EQ(string.type(), HeaderString::Type::Dynamic);
    const void* dynamic_buffer_address = string.getStringView().data();
    // Dynamic capacity in setCopy is 2x required by the size.
    // So to fill it exactly append 129 chars for a total of 258 chars.
    std::string large2(129, 'z');
    string.append(large2.c_str(), large2.size());
    EXPECT_EQ(string.type(), HeaderString::Type::Dynamic);
    // The actual buffer address should be the same as it was after
    // setCopy(large1), ensuring no reallocation occurred.
    EXPECT_EQ(string.getStringView().data(), dynamic_buffer_address);
    EXPECT_EQ(string.getStringView(), large1 + large2);
  }

  // Set integer, inline
  {
    HeaderString string;
    string.setInteger(123456789);
    EXPECT_EQ("123456789", string.getStringView());
    EXPECT_EQ(9U, string.size());
  }

  // Set integer, dynamic
  {
    HeaderString string;
    std::string large(129, 'a');
    string.append(large.c_str(), large.size());
    string.setInteger(123456789);
    EXPECT_EQ("123456789", string.getStringView());
    EXPECT_EQ(9U, string.size());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());
  }

  // Set static, switch to dynamic, back to static.
  {
    const std::string static_string = "hello world";
    HeaderString string;
    string.setReference(static_string);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(11U, string.size());
    EXPECT_EQ(HeaderString::Type::Reference, string.type());

    const std::string large(129, 'a');
    string.setCopy(large.c_str(), large.size());
    EXPECT_NE(string.getStringView().data(), large.c_str());
    EXPECT_EQ(HeaderString::Type::Dynamic, string.type());

    string.setReference(static_string);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(11U, string.size());
    EXPECT_EQ(HeaderString::Type::Reference, string.type());
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
  EXPECT_TRUE(headers.empty());
  EXPECT_EQ(0, headers.size());
  EXPECT_EQ(headers.byteSize().value(), 0);
  EXPECT_EQ(nullptr, headers.Host());
  headers.insertHost().value(std::string("hello"));
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(1, headers.size());
  EXPECT_EQ(":authority", headers.Host()->key().getStringView());
  EXPECT_EQ("hello", headers.Host()->value().getStringView());
  EXPECT_EQ("hello", headers.get(Headers::get().Host)->value().getStringView());
}

// Utility function for testing byteSize() against a manual byte count.
uint64_t countBytesForTest(const HeaderMapImpl& headers) {
  uint64_t byte_size = 0;
  headers.iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        auto* byte_size = static_cast<uint64_t*>(context);
        *byte_size += header.key().getStringView().size() + header.value().getStringView().size();
        return Http::HeaderMap::Iterate::Continue;
      },
      &byte_size);
  return byte_size;
}

TEST(HeaderMapImplTest, MoveIntoInline) {
  HeaderMapImpl headers;
  HeaderString key;
  key.setCopy(Headers::get().CacheControl.get());
  HeaderString value;
  value.setCopy("hello", 5);
  headers.addViaMove(std::move(key), std::move(value));
  EXPECT_EQ("cache-control", headers.CacheControl()->key().getStringView());
  EXPECT_EQ("hello", headers.CacheControl()->value().getStringView());

  HeaderString key2;
  key2.setCopy(Headers::get().CacheControl.get().c_str(), Headers::get().CacheControl.get().size());
  HeaderString value2;
  value2.setCopy("there", 5);
  headers.addViaMove(std::move(key2), std::move(value2));
  EXPECT_EQ("cache-control", headers.CacheControl()->key().getStringView());
  EXPECT_EQ("hello,there", headers.CacheControl()->value().getStringView());
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
}

TEST(HeaderMapImplTest, Remove) {
  HeaderMapImpl headers;

  // Add random header and then remove by name.
  LowerCaseString static_key("hello");
  std::string ref_value("value");
  headers.addReference(static_key, ref_value);
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
  EXPECT_EQ("value", headers.get(static_key)->value().getStringView());
  EXPECT_EQ(HeaderString::Type::Reference, headers.get(static_key)->value().type());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  headers.remove(static_key);
  EXPECT_EQ(nullptr, headers.get(static_key));
  EXPECT_EQ(0UL, headers.size());
  EXPECT_TRUE(headers.empty());
  EXPECT_EQ(headers.refreshByteSize(), 0);

  // Add and remove by inline.
  headers.insertContentLength().value(5);
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
  EXPECT_EQ("5", headers.ContentLength()->value().getStringView());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  headers.removeContentLength();
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_TRUE(headers.empty());
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));

  // Add inline and remove by name.
  headers.insertContentLength().value(5);
  EXPECT_EQ("5", headers.ContentLength()->value().getStringView());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
  headers.remove(Headers::get().ContentLength);
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_TRUE(headers.empty());
  EXPECT_EQ(headers.refreshByteSize(), 0);
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
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));

  // Test removing the first header, middle headers, and the end header.
  headers.removePrefix(LowerCaseString("x-prefix-"));
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
  EXPECT_EQ(nullptr, headers.get(key1));
  EXPECT_NE(nullptr, headers.get(key2));
  EXPECT_EQ(nullptr, headers.get(key3));
  EXPECT_NE(nullptr, headers.get(key4));
  EXPECT_EQ(nullptr, headers.get(key5));

  // Remove all headers.
  headers.refreshByteSize();
  headers.removePrefix(LowerCaseString(""));
  EXPECT_EQ(headers.byteSize().value(), 0);
  EXPECT_EQ(nullptr, headers.get(key2));
  EXPECT_EQ(nullptr, headers.get(key4));

  // Add inline and remove by regex
  headers.insertContentLength().value(5);
  EXPECT_EQ("5", headers.ContentLength()->value().getStringView());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
  headers.removePrefix(LowerCaseString("content"));
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(headers.refreshByteSize(), 0);
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
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));

  using MockCb = testing::MockFunction<void(const std::string&, const std::string&)>;

  {
    MockCb cb;

    InSequence seq;
    EXPECT_CALL(cb, Call("hello", "world"));
    EXPECT_CALL(cb, Call("olleh", "planet"));
    EXPECT_CALL(cb, Call("hello", "globe"));
    EXPECT_CALL(cb, Call("hello", "earth"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);
  }

  headers.setReference(key1, ref_value5); // set moves key to end

  {
    MockCb cb;

    InSequence seq;
    EXPECT_CALL(cb, Call("olleh", "planet"));
    EXPECT_CALL(cb, Call("hello", "blue marble"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);
  }
}

TEST(HeaderMapImplTest, DoubleInlineAdd) {
  {
    HeaderMapImpl headers;
    const std::string foo("foo");
    const std::string bar("bar");
    headers.addReference(Headers::get().ContentLength, foo);
    headers.addReference(Headers::get().ContentLength, bar);
    EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
    EXPECT_EQ("foo,bar", headers.ContentLength()->value().getStringView());
    EXPECT_EQ(1UL, headers.size());
  }
  {
    HeaderMapImpl headers;
    headers.addReferenceKey(Headers::get().ContentLength, "foo");
    headers.addReferenceKey(Headers::get().ContentLength, "bar");
    EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
    EXPECT_EQ("foo,bar", headers.ContentLength()->value().getStringView());
    EXPECT_EQ(1UL, headers.size());
  }
  {
    HeaderMapImpl headers;
    headers.addReferenceKey(Headers::get().ContentLength, 5);
    headers.addReferenceKey(Headers::get().ContentLength, 6);
    EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
    EXPECT_EQ("5,6", headers.ContentLength()->value().getStringView());
    EXPECT_EQ(1UL, headers.size());
  }
  {
    HeaderMapImpl headers;
    const std::string foo("foo");
    headers.addReference(Headers::get().ContentLength, foo);
    headers.addReferenceKey(Headers::get().ContentLength, 6);
    EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
    EXPECT_EQ("foo,6", headers.ContentLength()->value().getStringView());
    EXPECT_EQ(1UL, headers.size());
  }
}

// Per https://github.com/envoyproxy/envoy/issues/7488 make sure we don't
// combine set-cookie headers
TEST(HeaderMapImplTest, DoubleCookieAdd) {
  HeaderMapImpl headers;
  const std::string foo("foo");
  const std::string bar("bar");
  const LowerCaseString& set_cookie = Http::Headers::get().SetCookie;
  headers.addReference(set_cookie, foo);
  headers.addReference(set_cookie, bar);
  EXPECT_EQ(2UL, headers.size());
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));

  std::vector<absl::string_view> out;
  Http::HeaderUtility::getAllOfHeader(headers, "set-cookie", out);
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0], "foo");
  ASSERT_EQ(out[1], "bar");
}

TEST(HeaderMapImplTest, DoubleInlineSet) {
  HeaderMapImpl headers;
  headers.setReferenceKey(Headers::get().ContentType, "blah");
  headers.setReferenceKey(Headers::get().ContentType, "text/html");
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
  EXPECT_EQ("text/html", headers.ContentType()->value().getStringView());
  EXPECT_EQ(1UL, headers.size());
}

TEST(HeaderMapImplTest, AddReferenceKey) {
  HeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.addReferenceKey(foo, "world");
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
  EXPECT_NE("world", headers.get(foo)->value().getStringView().data());
  EXPECT_EQ("world", headers.get(foo)->value().getStringView());
}

TEST(HeaderMapImplTest, SetReferenceKey) {
  HeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.setReferenceKey(foo, "world");
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
  EXPECT_NE("world", headers.get(foo)->value().getStringView().data());
  EXPECT_EQ("world", headers.get(foo)->value().getStringView());
  headers.refreshByteSize();

  headers.setReferenceKey(foo, "monde");
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
  EXPECT_NE("monde", headers.get(foo)->value().getStringView().data());
  EXPECT_EQ("monde", headers.get(foo)->value().getStringView());
}

TEST(HeaderMapImplTest, AddCopy) {
  HeaderMapImpl headers;

  // Start with a string value.
  std::unique_ptr<LowerCaseString> lcKeyPtr(new LowerCaseString("hello"));
  headers.addCopy(*lcKeyPtr, "world");
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));

  const HeaderString& value = headers.get(*lcKeyPtr)->value();

  EXPECT_EQ("world", value.getStringView());
  EXPECT_EQ(5UL, value.size());

  lcKeyPtr.reset();

  const HeaderString& value2 = headers.get(LowerCaseString("hello"))->value();

  EXPECT_EQ("world", value2.getStringView());
  EXPECT_EQ(5UL, value2.size());
  EXPECT_EQ(value.getStringView(), value2.getStringView());
  EXPECT_EQ(1UL, headers.size());

  // Repeat with an int value.
  //
  // addReferenceKey and addCopy can both add multiple instances of a
  // given header, so we need to delete the old "hello" header.
  // Test that removing will return 0 byte size.
  headers.refreshByteSize();
  headers.remove(LowerCaseString("hello"));
  EXPECT_EQ(headers.byteSize().value(), 0);

  // Build "hello" with string concatenation to make it unlikely that the
  // compiler is just reusing the same string constant for everything.
  lcKeyPtr = std::make_unique<LowerCaseString>(std::string("he") + "llo");
  EXPECT_STREQ("hello", lcKeyPtr->get().c_str());

  headers.refreshByteSize();
  headers.addCopy(*lcKeyPtr, 42);
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));

  const HeaderString& value3 = headers.get(*lcKeyPtr)->value();

  EXPECT_EQ("42", value3.getStringView());
  EXPECT_EQ(2UL, value3.size());

  lcKeyPtr.reset();

  const HeaderString& value4 = headers.get(LowerCaseString("hello"))->value();

  EXPECT_EQ("42", value4.getStringView());
  EXPECT_EQ(2UL, value4.size());
  EXPECT_EQ(1UL, headers.size());

  // Here, again, we'll build yet another key string.
  LowerCaseString lcKey3(std::string("he") + "ll" + "o");
  EXPECT_STREQ("hello", lcKey3.get().c_str());

  EXPECT_EQ("42", headers.get(lcKey3)->value().getStringView());
  EXPECT_EQ(2UL, headers.get(lcKey3)->value().size());

  LowerCaseString cache_control("cache-control");
  headers.addCopy(cache_control, "max-age=1345");
  EXPECT_EQ("max-age=1345", headers.get(cache_control)->value().getStringView());
  EXPECT_EQ("max-age=1345", headers.CacheControl()->value().getStringView());
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
  headers.addCopy(cache_control, "public");
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
  EXPECT_EQ("max-age=1345,public", headers.get(cache_control)->value().getStringView());
  headers.addCopy(cache_control, "");
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
  EXPECT_EQ("max-age=1345,public", headers.get(cache_control)->value().getStringView());
  headers.addCopy(cache_control, 123);
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
  EXPECT_EQ("max-age=1345,public,123", headers.get(cache_control)->value().getStringView());
  headers.addCopy(cache_control, std::numeric_limits<uint64_t>::max());
  EXPECT_EQ("max-age=1345,public,123,18446744073709551615",
            headers.get(cache_control)->value().getStringView());
  EXPECT_EQ(headers.refreshByteSize(), countBytesForTest(headers));
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
  EXPECT_EQ(headers.byteSize().value(), countBytesForTest(headers));
  EXPECT_EQ("value", headers.get(static_key)->value().getStringView());
}

TEST(HeaderMapImplTest, Iterate) {
  TestHeaderMapImpl headers;
  headers.addCopy("hello", "world");
  headers.addCopy("foo", "xxx");
  headers.addCopy("world", "hello");
  LowerCaseString foo_key("foo");
  headers.setReferenceKey(foo_key, "bar"); // set moves key to end

  using MockCb = testing::MockFunction<void(const std::string&, const std::string&)>;
  MockCb cb;

  InSequence seq;
  EXPECT_CALL(cb, Call("hello", "world"));
  EXPECT_CALL(cb, Call("world", "hello"));
  EXPECT_CALL(cb, Call("foo", "bar"));
  headers.iterate(
      [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
        static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                         std::string(header.value().getStringView()));
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

  using MockCb = testing::MockFunction<void(const std::string&, const std::string&)>;
  MockCb cb;

  InSequence seq;
  EXPECT_CALL(cb, Call("world", "hello"));
  EXPECT_CALL(cb, Call("foo", "bar"));
  // no "hello"
  headers.iterateReverse(
      [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
        static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                         std::string(header.value().getStringView()));
        if (header.key().getStringView() != "foo") {
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
    EXPECT_EQ("5", entry->value().getStringView());
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
    EXPECT_EQ("/", headers.get(LowerCaseString(":path"))->value().getStringView());
    EXPECT_EQ("world", headers.get(LowerCaseString("hello"))->value().getStringView());
    EXPECT_EQ(nullptr, headers.get(LowerCaseString("foo")));
  }

  {
    TestHeaderMapImpl headers{{":path", "/"}, {"hello", "world"}};
    headers.get(LowerCaseString(":path"))->value(std::string("/new_path"));
    EXPECT_EQ("/new_path", headers.get(LowerCaseString(":path"))->value().getStringView());
    headers.get(LowerCaseString("hello"))->value(std::string("world2"));
    EXPECT_EQ("world2", headers.get(LowerCaseString("hello"))->value().getStringView());
    EXPECT_EQ(nullptr, headers.get(LowerCaseString("foo")));
  }
}

TEST(HeaderMapImplTest, TestAppendHeader) {
  // Test appending to a string with a value.
  {
    HeaderString value1;
    value1.setCopy("some;", 5);
    HeaderMapImpl::appendToHeader(value1, "test");
    EXPECT_EQ(value1, "some;,test");
  }

  // Test appending to an empty string.
  {
    HeaderString value2;
    HeaderMapImpl::appendToHeader(value2, "my tag data");
    EXPECT_EQ(value2, "my tag data");
  }

  // Test empty data case.
  {
    HeaderString value3;
    value3.setCopy("empty", 5);
    HeaderMapImpl::appendToHeader(value3, "");
    EXPECT_EQ(value3, "empty");
  }
  // Regression test for appending to an empty string with a short string, then
  // setting integer.
  {
    const std::string empty;
    HeaderString value4(empty);
    HeaderMapImpl::appendToHeader(value4, " ");
    value4.setInteger(0);
    EXPECT_EQ("0", value4.getStringView());
    EXPECT_EQ(1U, value4.size());
  }
}

TEST(HeaderMapImplDeathTest, TestHeaderLengthChecks) {
  HeaderString value;
  value.setCopy("some;", 5);
  EXPECT_DEATH_LOG_TO_STDERR(value.append(nullptr, std::numeric_limits<uint32_t>::max()),
                             "Trying to allocate overly large headers.");

  std::string source("hello");
  HeaderString reference;
  reference.setReference(source);
  EXPECT_DEATH_LOG_TO_STDERR(reference.append(nullptr, std::numeric_limits<uint32_t>::max()),
                             "Trying to allocate overly large headers.");
}

TEST(HeaderMapImplTest, PseudoHeaderOrder) {
  using MockCb = testing::MockFunction<void(const std::string&, const std::string&)>;
  MockCb cb;

  {
    LowerCaseString foo("hello");
    Http::TestHeaderMapImpl headers{};
    EXPECT_EQ(headers.refreshByteSize(), 0);
    EXPECT_EQ(0UL, headers.size());
    EXPECT_TRUE(headers.empty());

    headers.addReferenceKey(foo, "world");
    EXPECT_EQ(1UL, headers.size());
    EXPECT_FALSE(headers.empty());

    headers.setReferenceKey(Headers::get().ContentType, "text/html");
    EXPECT_EQ(2UL, headers.size());
    EXPECT_FALSE(headers.empty());

    // Pseudo header gets inserted before non-pseudo headers
    headers.setReferenceKey(Headers::get().Method, "PUT");
    EXPECT_EQ(3UL, headers.size());
    EXPECT_FALSE(headers.empty());

    InSequence seq;
    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call("hello", "world"));
    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);

    // Removal of the header before which pseudo-headers are inserted
    headers.remove(foo);
    EXPECT_EQ(2UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);

    // Next pseudo-header goes after other pseudo-headers, but before normal headers
    headers.setReferenceKey(Headers::get().Path, "/test");
    EXPECT_EQ(3UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));
    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);

    // Removing the last normal header
    headers.remove(Headers::get().ContentType);
    EXPECT_EQ(2UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);

    // Adding a new pseudo-header after removing the last normal header
    headers.setReferenceKey(Headers::get().Host, "host");
    EXPECT_EQ(3UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));
    EXPECT_CALL(cb, Call(":authority", "host"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);

    // Adding the first normal header
    headers.setReferenceKey(Headers::get().ContentType, "text/html");
    EXPECT_EQ(4UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));
    EXPECT_CALL(cb, Call(":authority", "host"));
    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);

    // Removing all pseudo-headers
    headers.remove(Headers::get().Path);
    headers.remove(Headers::get().Method);
    headers.remove(Headers::get().Host);
    EXPECT_EQ(1UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);

    // Removing all headers
    headers.remove(Headers::get().ContentType);
    EXPECT_EQ(0UL, headers.size());
    EXPECT_TRUE(headers.empty());

    // Adding a lone pseudo-header
    headers.setReferenceKey(Headers::get().Status, "200");
    EXPECT_EQ(1UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":status", "200"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);
  }

  // Starting with a normal header
  {
    Http::TestHeaderMapImpl headers{{"content-type", "text/plain"},
                                    {":method", "GET"},
                                    {":path", "/"},
                                    {"hello", "world"},
                                    {":authority", "host"}};

    InSequence seq;
    EXPECT_CALL(cb, Call(":method", "GET"));
    EXPECT_CALL(cb, Call(":path", "/"));
    EXPECT_CALL(cb, Call(":authority", "host"));
    EXPECT_CALL(cb, Call("content-type", "text/plain"));
    EXPECT_CALL(cb, Call("hello", "world"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);
  }

  // Starting with a pseudo-header
  {
    Http::TestHeaderMapImpl headers{{":path", "/"},
                                    {"content-type", "text/plain"},
                                    {":method", "GET"},
                                    {"hello", "world"},
                                    {":authority", "host"}};

    InSequence seq;
    EXPECT_CALL(cb, Call(":path", "/"));
    EXPECT_CALL(cb, Call(":method", "GET"));
    EXPECT_CALL(cb, Call(":authority", "host"));
    EXPECT_CALL(cb, Call("content-type", "text/plain"));
    EXPECT_CALL(cb, Call("hello", "world"));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* cb_v) -> HeaderMap::Iterate {
          static_cast<MockCb*>(cb_v)->Call(std::string(header.key().getStringView()),
                                           std::string(header.value().getStringView()));
          return HeaderMap::Iterate::Continue;
        },
        &cb);
  }
}

// Validate that TestHeaderMapImpl copy construction and assignment works. This is a
// regression for where we were missing a valid copy constructor and had the
// default (dangerous) move semantics takeover.
TEST(HeaderMapImplTest, TestHeaderMapImplyCopy) {
  TestHeaderMapImpl foo;
  foo.addCopy(LowerCaseString("foo"), "bar");
  auto headers = std::make_unique<TestHeaderMapImpl>(foo);
  EXPECT_EQ("bar", headers->get(LowerCaseString("foo"))->value().getStringView());
  TestHeaderMapImpl baz{{"foo", "baz"}};
  baz = *headers;
  EXPECT_EQ("bar", baz.get(LowerCaseString("foo"))->value().getStringView());
  const TestHeaderMapImpl& baz2 = baz;
  baz = baz2;
  EXPECT_EQ("bar", baz.get(LowerCaseString("foo"))->value().getStringView());
}

TEST(HeaderMapImplTest, TestInlineHeaderAdd) {
  TestHeaderMapImpl foo;
  foo.addCopy(":path", "GET");
  EXPECT_EQ(foo.size(), 1);
  EXPECT_TRUE(foo.Path() != nullptr);
}

} // namespace Http
} // namespace Envoy
