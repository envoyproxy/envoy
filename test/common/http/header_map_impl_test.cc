#include <algorithm>
#include <memory>
#include <string>

#include "common/http/header_list_view.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"

#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::ElementsAre;
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
    string.setCopy("hello");
    EXPECT_FALSE(string.isReference());
    HeaderString string2(std::move(string));
    EXPECT_TRUE(string.empty()); // NOLINT(bugprone-use-after-move)
    EXPECT_FALSE(string.isReference());
    EXPECT_FALSE(string2.isReference());
    string.append("world", 5);
    EXPECT_EQ("world", string.getStringView());
    EXPECT_EQ(5UL, string.size());
    EXPECT_EQ("hello", string2.getStringView());
    EXPECT_EQ(5UL, string2.size());
  }

  // Inline move large constructor
  {
    std::string large(4096, 'a');
    HeaderString string;
    string.setCopy(large);
    EXPECT_FALSE(string.isReference());
    HeaderString string2(std::move(string));
    EXPECT_TRUE(string.empty()); // NOLINT(bugprone-use-after-move)
    EXPECT_FALSE(string.isReference());
    EXPECT_FALSE(string2.isReference());
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
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ("5", string.getStringView());
  }

  // Static to inline string.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    string.setCopy(static_string);
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ("HELLO", string.getStringView());
  }

  // Inline rtrim removes trailing whitespace only.
  {
    const std::string data_with_leading_lws = " \t\f\v  data";
    const std::string data_with_leading_and_trailing_lws = data_with_leading_lws + " \t\f\v";
    HeaderString string;
    string.append(data_with_leading_and_trailing_lws.data(),
                  data_with_leading_and_trailing_lws.size());
    EXPECT_EQ(data_with_leading_and_trailing_lws, string.getStringView());
    string.rtrim();
    EXPECT_NE(data_with_leading_and_trailing_lws, string.getStringView());
    EXPECT_EQ(data_with_leading_lws, string.getStringView());
  }

  // Static clear() does nothing.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_TRUE(string.isReference());
    string.clear();
    EXPECT_TRUE(string.isReference());
    EXPECT_EQ("HELLO", string.getStringView());
  }

  // Static to append.
  {
    std::string static_string("HELLO");
    HeaderString string(static_string);
    EXPECT_TRUE(string.isReference());
    string.append("a", 1);
    EXPECT_EQ("HELLOa", string.getStringView());
  }

  // Copy inline
  {
    HeaderString string;
    string.setCopy("hello");
    EXPECT_EQ("hello", string.getStringView());
    EXPECT_EQ(5U, string.size());
  }

  // Copy dynamic
  {
    HeaderString string;
    std::string large_value(4096, 'a');
    string.setCopy(large_value);
    EXPECT_EQ(large_value, string.getStringView());
    EXPECT_NE(large_value.c_str(), string.getStringView().data());
    EXPECT_EQ(4096U, string.size());
  }

  // Copy twice dynamic
  {
    HeaderString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1);
    std::string large_value2(2048, 'b');
    string.setCopy(large_value2);
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(2048U, string.size());
  }

  // Copy twice dynamic with reallocate
  {
    HeaderString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1);
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2);
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(16384U, string.size());
  }

  // Copy twice inline to dynamic
  {
    HeaderString string;
    std::string large_value1(16, 'a');
    string.setCopy(large_value1);
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2);
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
    string.setCopy(large);
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ(string.getStringView(), large);
  }

  // Copy, exactly filling dynamic capacity
  //
  // ASAN should catch a write one past the end of the inline buffer. This test
  // forces a dynamic buffer with one copy and then fills it with the next.
  {
    HeaderString string;
    // Force dynamic vector allocation with setCopy of inline buffer size + 1.
    std::string large1(129, 'z');
    string.setCopy(large1);
    EXPECT_FALSE(string.isReference());
    // Dynamic capacity in setCopy is 2x required by the size.
    // So to fill it exactly setCopy with a total of 256 chars.
    std::string large2(256, 'z');
    string.setCopy(large2);
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ(string.getStringView(), large2);
  }

  // Append, small buffer to inline
  {
    HeaderString string;
    std::string test(128, 'a');
    string.append(test.c_str(), test.size());
    EXPECT_FALSE(string.isReference());
    string.append("a", 1);
    EXPECT_FALSE(string.isReference());
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

  // Append, realloc close to limit with small buffer.
  {
    HeaderString string;
    std::string large(129, 'a');
    string.append(large.c_str(), large.size());
    EXPECT_FALSE(string.isReference());
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
    // Force dynamic allocation with setCopy of inline buffer size + 1.
    std::string large1(129, 'z');
    string.setCopy(large1);
    EXPECT_FALSE(string.isReference());
    // Dynamic capacity in setCopy is 2x required by the size.
    // So to fill it exactly append 127 chars for a total of 256 chars.
    std::string large2(127, 'z');
    string.append(large2.c_str(), large2.size());
    EXPECT_FALSE(string.isReference());
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
    EXPECT_FALSE(string.isReference());
  }

  // Set static, switch to inline, back to static.
  {
    const std::string static_string = "hello world";
    HeaderString string;
    string.setReference(static_string);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(11U, string.size());
    EXPECT_TRUE(string.isReference());

    const std::string large(129, 'a');
    string.setCopy(large);
    EXPECT_NE(string.getStringView().data(), large.c_str());
    EXPECT_FALSE(string.isReference());

    string.setReference(static_string);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(11U, string.size());
    EXPECT_TRUE(string.isReference());
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

  // inlineTransform
  {
    const std::string static_string = "HELLO";
    HeaderString string;
    string.setCopy(static_string);
    string.inlineTransform([](char c) { return static_cast<uint8_t>(tolower(c)); });
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ(5U, string.size());
    EXPECT_EQ(string.getStringView(), "hello");
    string.inlineTransform(toupper);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(5U, string.size());
    EXPECT_FALSE(string.isReference());
  }
}

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    custom_header_1(Http::LowerCaseString{"foo_custom_header"});
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    custom_header_1_copy(Http::LowerCaseString{"foo_custom_header"});

class HeaderMapImplTest : public testing::TestWithParam<uint32_t> {
public:
  HeaderMapImplTest() {
    // Set the lazy map threshold using the test parameter.
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.http.headermap.lazy_map_min_size", absl::StrCat(GetParam())}});
  }

  static std::string testParamsToString(const ::testing::TestParamInfo<uint32_t>& params) {
    return absl::StrCat(params.param);
  }

  TestScopedRuntime runtime;
};

INSTANTIATE_TEST_SUITE_P(HeaderMapThreshold, HeaderMapImplTest,
                         testing::Values(0, 1, std::numeric_limits<uint32_t>::max()),
                         HeaderMapImplTest::testParamsToString);

// Make sure that the same header registered twice points to the same location.
TEST_P(HeaderMapImplTest, CustomRegisteredHeaders) {
  TestRequestHeaderMapImpl headers;
  EXPECT_EQ(custom_header_1.handle(), custom_header_1_copy.handle());
  EXPECT_EQ(nullptr, headers.getInline(custom_header_1.handle()));
  EXPECT_EQ(nullptr, headers.getInline(custom_header_1_copy.handle()));
  headers.setInline(custom_header_1.handle(), 42);
  EXPECT_EQ("42", headers.getInlineValue(custom_header_1_copy.handle()));
  EXPECT_EQ("foo_custom_header",
            headers.getInline(custom_header_1.handle())->key().getStringView());
}

#define TEST_INLINE_HEADER_FUNCS(name)                                                             \
  header_map->addCopy(Headers::get().name, #name);                                                 \
  EXPECT_EQ(header_map->name()->value().getStringView(), #name);                                   \
  header_map->remove##name();                                                                      \
  EXPECT_EQ(nullptr, header_map->name());

#define TEST_INLINE_STRING_HEADER_FUNCS(name)                                                      \
  TEST_INLINE_HEADER_FUNCS(name)                                                                   \
  header_map->set##name(#name);                                                                    \
  EXPECT_EQ(header_map->get(Headers::get().name)[0]->value().getStringView(), #name);

#define TEST_INLINE_NUMERIC_HEADER_FUNCS(name)                                                     \
  TEST_INLINE_HEADER_FUNCS(name)                                                                   \
  header_map->set##name(1);                                                                        \
  EXPECT_EQ(header_map->get(Headers::get().name)[0]->value().getStringView(), 1);

// Make sure that the O(1) headers are wired up properly.
TEST_P(HeaderMapImplTest, AllInlineHeaders) {
  {
    auto header_map = RequestHeaderMapImpl::create();
    INLINE_REQ_STRING_HEADERS(TEST_INLINE_STRING_HEADER_FUNCS)
    INLINE_REQ_RESP_STRING_HEADERS(TEST_INLINE_STRING_HEADER_FUNCS)
  }
  {
      // No request trailer O(1) headers.
  } {
    auto header_map = ResponseHeaderMapImpl::create();
    INLINE_RESP_STRING_HEADERS(TEST_INLINE_STRING_HEADER_FUNCS)
    INLINE_REQ_RESP_STRING_HEADERS(TEST_INLINE_STRING_HEADER_FUNCS)
    INLINE_RESP_STRING_HEADERS_TRAILERS(TEST_INLINE_STRING_HEADER_FUNCS)
  }
  {
    auto header_map = ResponseTrailerMapImpl::create();
    INLINE_RESP_STRING_HEADERS_TRAILERS(TEST_INLINE_STRING_HEADER_FUNCS)
  }
}

TEST_P(HeaderMapImplTest, InlineInsert) {
  TestRequestHeaderMapImpl headers;
  EXPECT_TRUE(headers.empty());
  EXPECT_EQ(0, headers.size());
  EXPECT_EQ(nullptr, headers.Host());
  headers.setHost("hello");
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(1, headers.size());
  EXPECT_EQ(":authority", headers.Host()->key().getStringView());
  EXPECT_EQ("hello", headers.getHostValue());
  EXPECT_EQ("hello", headers.get(Headers::get().Host)[0]->value().getStringView());
}

TEST_P(HeaderMapImplTest, InlineAppend) {
  {
    TestRequestHeaderMapImpl headers;
    // Create via header and append.
    headers.setVia("");
    headers.appendVia("1.0 fred", ",");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred");
    headers.appendVia("1.1 nowhere.com", ",");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred,1.1 nowhere.com");
  }
  {
    // Append to via header without explicitly creating first.
    TestRequestHeaderMapImpl headers;
    headers.appendVia("1.0 fred", ",");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred");
    headers.appendVia("1.1 nowhere.com", ",");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred,1.1 nowhere.com");
  }
  {
    // Custom delimiter.
    TestRequestHeaderMapImpl headers;
    headers.setVia("");
    headers.appendVia("1.0 fred", ", ");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred");
    headers.appendVia("1.1 nowhere.com", ", ");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred, 1.1 nowhere.com");
  }
  {
    // Append and then later set.
    TestRequestHeaderMapImpl headers;
    headers.appendVia("1.0 fred", ",");
    headers.appendVia("1.1 nowhere.com", ",");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred,1.1 nowhere.com");
    headers.setVia("2.0 override");
    EXPECT_EQ(headers.getViaValue(), "2.0 override");
  }
}

TEST_P(HeaderMapImplTest, MoveIntoInline) {
  TestRequestHeaderMapImpl headers;
  HeaderString key;
  key.setCopy(Headers::get().EnvoyRetryOn.get());
  HeaderString value;
  value.setCopy("hello");
  headers.addViaMove(std::move(key), std::move(value));
  EXPECT_EQ("x-envoy-retry-on", headers.EnvoyRetryOn()->key().getStringView());
  EXPECT_EQ("hello", headers.getEnvoyRetryOnValue());

  HeaderString key2;
  key2.setCopy(Headers::get().EnvoyRetryOn.get());
  HeaderString value2;
  value2.setCopy("there");
  headers.addViaMove(std::move(key2), std::move(value2));
  EXPECT_EQ("x-envoy-retry-on", headers.EnvoyRetryOn()->key().getStringView());
  EXPECT_EQ("hello,there", headers.getEnvoyRetryOnValue());
}

TEST_P(HeaderMapImplTest, Remove) {
  TestRequestHeaderMapImpl headers;

  // Add random header and then remove by name.
  LowerCaseString static_key("hello");
  std::string ref_value("value");
  headers.addReference(static_key, ref_value);
  EXPECT_EQ("value", headers.get(static_key)[0]->value().getStringView());
  EXPECT_TRUE(headers.get(static_key)[0]->value().isReference());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(1UL, headers.remove(static_key));
  EXPECT_TRUE(headers.get(static_key).empty());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_TRUE(headers.empty());

  // Add and remove by inline.
  EXPECT_EQ(0UL, headers.removeContentLength());
  headers.setContentLength(5);
  EXPECT_EQ("5", headers.getContentLengthValue());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(1UL, headers.removeContentLength());
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_TRUE(headers.empty());

  // Add inline and remove by name.
  headers.setContentLength(5);
  EXPECT_EQ("5", headers.getContentLengthValue());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(1UL, headers.remove(Headers::get().ContentLength));
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_TRUE(headers.empty());

  // Try to remove nonexistent headers.
  EXPECT_EQ(0UL, headers.remove(static_key));
  EXPECT_EQ(0UL, headers.remove(Headers::get().ContentLength));
}

TEST_P(HeaderMapImplTest, RemoveHost) {
  TestRequestHeaderMapImpl headers;
  headers.setHost("foo");
  EXPECT_EQ("foo", headers.get_("host"));
  EXPECT_EQ("foo", headers.get_(":authority"));
  // Make sure that when we remove by "host" without using the inline functions, the mapping to
  // ":authority" still takes place.
  // https://github.com/envoyproxy/envoy/pull/12160
  EXPECT_EQ(1UL, headers.remove("host"));
  EXPECT_EQ("", headers.get_("host"));
  EXPECT_EQ("", headers.get_(":authority"));
  EXPECT_EQ(nullptr, headers.Host());
}

TEST_P(HeaderMapImplTest, RemoveIf) {
  LowerCaseString key1 = LowerCaseString("X-postfix-foo");
  LowerCaseString key2 = LowerCaseString("X-postfix-");
  LowerCaseString key3 = LowerCaseString("x-postfix-eep");

  {
    TestRequestHeaderMapImpl headers;
    headers.addReference(key1, "value");
    headers.addReference(key2, "value");
    headers.addReference(key3, "value");

    EXPECT_EQ(0UL, headers.removeIf([](const HeaderEntry&) -> bool { return false; }));

    EXPECT_EQ(2UL, headers.removeIf([](const HeaderEntry& entry) -> bool {
      return absl::EndsWith(entry.key().getStringView(), "foo") ||
             absl::EndsWith(entry.key().getStringView(), "eep");
    }));

    TestRequestHeaderMapImpl expected{{"X-postfix-", "value"}};
    EXPECT_EQ(expected, headers);
  }

  // Test multiple entries with same key but different value.
  {
    TestRequestHeaderMapImpl headers;
    headers.addReference(key1, "valueA");
    headers.addReference(key1, "valueB");
    headers.addReference(key1, "valueC");
    headers.addReference(key2, "valueB");
    headers.addReference(key3, "valueC");

    EXPECT_EQ(5UL, headers.size());
    EXPECT_EQ(2UL, headers.removeIf([](const HeaderEntry& entry) -> bool {
      return absl::EndsWith(entry.value().getStringView(), "B");
    }));

    // Make sure key1 other values still exist.
    TestRequestHeaderMapImpl expected{
        {key1.get(), "valueA"}, {key1.get(), "valueC"}, {key3.get(), "valueC"}};
    EXPECT_EQ(expected, headers);
  }
}

TEST_P(HeaderMapImplTest, RemovePrefix) {
  // These will match.
  LowerCaseString key1 = LowerCaseString("X-prefix-foo");
  LowerCaseString key3 = LowerCaseString("X-Prefix-");
  LowerCaseString key5 = LowerCaseString("x-prefix-eep");
  // These will not.
  LowerCaseString key2 = LowerCaseString(" x-prefix-foo");
  LowerCaseString key4 = LowerCaseString("y-x-prefix-foo");

  TestRequestHeaderMapImpl headers;
  headers.addReference(key1, "value");
  headers.addReference(key2, "value");
  headers.addReference(key3, "value");
  headers.addReference(key4, "value");
  headers.addReference(key5, "value");

  // Test removing the first header, middle headers, and the end header.
  EXPECT_EQ(3UL, headers.removePrefix(LowerCaseString("x-prefix-")));
  EXPECT_TRUE(headers.get(key1).empty());
  EXPECT_FALSE(headers.get(key2).empty());
  EXPECT_TRUE(headers.get(key3).empty());
  EXPECT_FALSE(headers.get(key4).empty());
  EXPECT_TRUE(headers.get(key5).empty());

  // Try to remove headers with no prefix match.
  EXPECT_EQ(0UL, headers.removePrefix(LowerCaseString("foo")));

  // Remove all headers.
  EXPECT_EQ(2UL, headers.removePrefix(LowerCaseString("")));
  EXPECT_TRUE(headers.get(key2).empty());
  EXPECT_TRUE(headers.get(key4).empty());

  // Add inline and remove by prefix
  headers.setContentLength(5);
  EXPECT_EQ("5", headers.getContentLengthValue());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  EXPECT_EQ(1UL, headers.removePrefix(LowerCaseString("content")));
  EXPECT_EQ(nullptr, headers.ContentLength());
}

class HeaderAndValueCb
    : public testing::MockFunction<void(const std::string&, const std::string&)> {
public:
  HeaderMap::ConstIterateCb asIterateCb() {
    return [this](const Http::HeaderEntry& header) -> HeaderMap::Iterate {
      Call(std::string(header.key().getStringView()), std::string(header.value().getStringView()));
      return HeaderMap::Iterate::Continue;
    };
  }
};

TEST_P(HeaderMapImplTest, SetRemovesAllValues) {
  TestRequestHeaderMapImpl headers;

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

  {
    HeaderAndValueCb cb;

    InSequence seq;
    EXPECT_CALL(cb, Call("hello", "world"));
    EXPECT_CALL(cb, Call("olleh", "planet"));
    EXPECT_CALL(cb, Call("hello", "globe"));
    EXPECT_CALL(cb, Call("hello", "earth"));

    headers.iterate(cb.asIterateCb());
  }

  headers.setReference(key1, ref_value5); // set moves key to end

  {
    HeaderAndValueCb cb;

    InSequence seq;
    EXPECT_CALL(cb, Call("olleh", "planet"));
    EXPECT_CALL(cb, Call("hello", "blue marble"));

    headers.iterate(cb.asIterateCb());
  }
}

TEST_P(HeaderMapImplTest, DoubleInlineAdd) {
  {
    TestRequestHeaderMapImpl headers;
    const std::string foo("foo");
    const std::string bar("bar");
    headers.addReference(Headers::get().ContentLength, foo);
    headers.addReference(Headers::get().ContentLength, bar);
    EXPECT_EQ("foo,bar", headers.getContentLengthValue());
    EXPECT_EQ(1UL, headers.size());
  }
  {
    TestRequestHeaderMapImpl headers;
    headers.addReferenceKey(Headers::get().ContentLength, "foo");
    headers.addReferenceKey(Headers::get().ContentLength, "bar");
    EXPECT_EQ("foo,bar", headers.getContentLengthValue());
    EXPECT_EQ(1UL, headers.size());
  }
  {
    TestRequestHeaderMapImpl headers;
    headers.addReferenceKey(Headers::get().ContentLength, 5);
    headers.addReferenceKey(Headers::get().ContentLength, 6);
    EXPECT_EQ("5,6", headers.getContentLengthValue());
    EXPECT_EQ(1UL, headers.size());
  }
  {
    TestRequestHeaderMapImpl headers;
    const std::string foo("foo");
    headers.addReference(Headers::get().ContentLength, foo);
    headers.addReferenceKey(Headers::get().ContentLength, 6);
    EXPECT_EQ("foo,6", headers.getContentLengthValue());
    EXPECT_EQ(1UL, headers.size());
  }
}

// Per https://github.com/envoyproxy/envoy/issues/7488 make sure we don't
// combine set-cookie headers
TEST_P(HeaderMapImplTest, DoubleCookieAdd) {
  TestRequestHeaderMapImpl headers;
  const std::string foo("foo");
  const std::string bar("bar");
  const LowerCaseString& set_cookie = Http::Headers::get().SetCookie;
  headers.addReference(set_cookie, foo);
  headers.addReference(set_cookie, bar);
  EXPECT_EQ(2UL, headers.size());

  const auto set_cookie_value = headers.get(LowerCaseString("set-cookie"));
  ASSERT_EQ(set_cookie_value.size(), 2);
  ASSERT_EQ(set_cookie_value[0]->value().getStringView(), "foo");
  ASSERT_EQ(set_cookie_value[1]->value().getStringView(), "bar");
}

TEST_P(HeaderMapImplTest, DoubleInlineSet) {
  TestRequestHeaderMapImpl headers;
  headers.setReferenceKey(Headers::get().ContentType, "blah");
  headers.setReferenceKey(Headers::get().ContentType, "text/html");
  EXPECT_EQ("text/html", headers.getContentTypeValue());
  EXPECT_EQ(1UL, headers.size());
}

TEST_P(HeaderMapImplTest, AddReferenceKey) {
  TestRequestHeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.addReferenceKey(foo, "world");
  EXPECT_NE("world", headers.get(foo)[0]->value().getStringView().data());
  EXPECT_EQ("world", headers.get(foo)[0]->value().getStringView());
}

TEST_P(HeaderMapImplTest, SetReferenceKey) {
  TestRequestHeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.setReferenceKey(foo, "world");
  EXPECT_NE("world", headers.get(foo)[0]->value().getStringView().data());
  EXPECT_EQ("world", headers.get(foo)[0]->value().getStringView());

  headers.setReferenceKey(foo, "monde");
  EXPECT_NE("monde", headers.get(foo)[0]->value().getStringView().data());
  EXPECT_EQ("monde", headers.get(foo)[0]->value().getStringView());
}

TEST_P(HeaderMapImplTest, SetCopyOldBehavior) {
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.http_set_copy_replace_all_headers", "false"}});

  TestRequestHeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.setCopy(foo, "world");
  EXPECT_EQ("world", headers.get(foo)[0]->value().getStringView());

  // Overwrite value.
  headers.setCopy(foo, "monde");
  EXPECT_EQ("monde", headers.get(foo)[0]->value().getStringView());

  // Add another foo header.
  headers.addCopy(foo, "monde2");
  EXPECT_EQ(headers.size(), 2);

  // Only the first foo header is overridden.
  headers.setCopy(foo, "override-monde");
  EXPECT_EQ(headers.size(), 2);

  HeaderAndValueCb cb;

  InSequence seq;
  EXPECT_CALL(cb, Call("hello", "override-monde"));
  EXPECT_CALL(cb, Call("hello", "monde2"));
  headers.iterate(cb.asIterateCb());

  // Test setting an empty string and then overriding.
  EXPECT_EQ(2UL, headers.remove(foo));
  EXPECT_EQ(headers.size(), 0);
  const std::string empty;
  headers.setCopy(foo, empty);
  EXPECT_EQ(headers.size(), 1);
  headers.setCopy(foo, "not-empty");
  EXPECT_EQ(headers.get(foo)[0]->value().getStringView(), "not-empty");

  // Use setCopy with inline headers both indirectly and directly.
  headers.clear();
  EXPECT_EQ(headers.size(), 0);
  headers.setCopy(Headers::get().Path, "/");
  EXPECT_EQ(headers.size(), 1);
  EXPECT_EQ(headers.getPathValue(), "/");
  headers.setPath("/foo");
  EXPECT_EQ(headers.size(), 1);
  EXPECT_EQ(headers.getPathValue(), "/foo");
}

TEST_P(HeaderMapImplTest, SetCopyNewBehavior) {
  TestRequestHeaderMapImpl headers;
  LowerCaseString foo("hello");
  headers.setCopy(foo, "world");
  EXPECT_EQ("world", headers.get(foo)[0]->value().getStringView());

  // Overwrite value.
  headers.setCopy(foo, "monde");
  EXPECT_EQ("monde", headers.get(foo)[0]->value().getStringView());

  // Add another foo header.
  headers.addCopy(foo, "monde2");
  EXPECT_EQ(headers.size(), 2);

  // The foo header is overridden.
  headers.setCopy(foo, "override-monde");
  EXPECT_EQ(headers.size(), 1);

  HeaderAndValueCb cb;

  InSequence seq;
  EXPECT_CALL(cb, Call("hello", "override-monde"));
  headers.iterate(cb.asIterateCb());

  // Test setting an empty string and then overriding.
  EXPECT_EQ(1UL, headers.remove(foo));
  EXPECT_EQ(headers.size(), 0);
  const std::string empty;
  headers.setCopy(foo, empty);
  EXPECT_EQ(headers.size(), 1);
  headers.setCopy(foo, "not-empty");
  EXPECT_EQ(headers.get(foo)[0]->value().getStringView(), "not-empty");

  // Use setCopy with inline headers both indirectly and directly.
  headers.clear();
  EXPECT_EQ(headers.size(), 0);
  headers.setCopy(Headers::get().Path, "/");
  EXPECT_EQ(headers.size(), 1);
  EXPECT_EQ(headers.getPathValue(), "/");
  headers.setPath("/foo");
  EXPECT_EQ(headers.size(), 1);
  EXPECT_EQ(headers.getPathValue(), "/foo");
}

TEST_P(HeaderMapImplTest, AddCopy) {
  TestRequestHeaderMapImpl headers;

  // Start with a string value.
  std::unique_ptr<LowerCaseString> lcKeyPtr(new LowerCaseString("hello"));
  headers.addCopy(*lcKeyPtr, "world");

  const HeaderString& value = headers.get(*lcKeyPtr)[0]->value();

  EXPECT_EQ("world", value.getStringView());
  EXPECT_EQ(5UL, value.size());

  lcKeyPtr.reset();

  const HeaderString& value2 = headers.get(LowerCaseString("hello"))[0]->value();

  EXPECT_EQ("world", value2.getStringView());
  EXPECT_EQ(5UL, value2.size());
  EXPECT_EQ(value.getStringView(), value2.getStringView());
  EXPECT_EQ(1UL, headers.size());

  // Repeat with an int value.
  //
  // addReferenceKey and addCopy can both add multiple instances of a
  // given header, so we need to delete the old "hello" header.
  // Test that removing will return 0 byte size.
  EXPECT_EQ(1UL, headers.remove(LowerCaseString("hello")));
  EXPECT_EQ(headers.byteSize(), 0);

  // Build "hello" with string concatenation to make it unlikely that the
  // compiler is just reusing the same string constant for everything.
  lcKeyPtr = std::make_unique<LowerCaseString>(std::string("he") + "llo");
  EXPECT_STREQ("hello", lcKeyPtr->get().c_str());

  headers.addCopy(*lcKeyPtr, 42);

  const HeaderString& value3 = headers.get(*lcKeyPtr)[0]->value();

  EXPECT_EQ("42", value3.getStringView());
  EXPECT_EQ(2UL, value3.size());

  lcKeyPtr.reset();

  const HeaderString& value4 = headers.get(LowerCaseString("hello"))[0]->value();

  EXPECT_EQ("42", value4.getStringView());
  EXPECT_EQ(2UL, value4.size());
  EXPECT_EQ(1UL, headers.size());

  // Here, again, we'll build yet another key string.
  LowerCaseString lcKey3(std::string("he") + "ll" + "o");
  EXPECT_STREQ("hello", lcKey3.get().c_str());

  EXPECT_EQ("42", headers.get(lcKey3)[0]->value().getStringView());
  EXPECT_EQ(2UL, headers.get(lcKey3)[0]->value().size());

  LowerCaseString envoy_retry_on("x-envoy-retry-on");
  headers.addCopy(envoy_retry_on, "max-age=1345");
  EXPECT_EQ("max-age=1345", headers.get(envoy_retry_on)[0]->value().getStringView());
  EXPECT_EQ("max-age=1345", headers.getEnvoyRetryOnValue());
  headers.addCopy(envoy_retry_on, "public");
  EXPECT_EQ("max-age=1345,public", headers.get(envoy_retry_on)[0]->value().getStringView());
  headers.addCopy(envoy_retry_on, "");
  EXPECT_EQ("max-age=1345,public", headers.get(envoy_retry_on)[0]->value().getStringView());
  headers.addCopy(envoy_retry_on, 123);
  EXPECT_EQ("max-age=1345,public,123", headers.get(envoy_retry_on)[0]->value().getStringView());
  headers.addCopy(envoy_retry_on, std::numeric_limits<uint64_t>::max());
  EXPECT_EQ("max-age=1345,public,123,18446744073709551615",
            headers.get(envoy_retry_on)[0]->value().getStringView());
}

TEST_P(HeaderMapImplTest, Equality) {
  TestRequestHeaderMapImpl headers1;
  TestRequestHeaderMapImpl headers2;
  EXPECT_EQ(headers1, headers2);

  headers1.addCopy(LowerCaseString("hello"), "world");
  EXPECT_FALSE(headers1 == headers2);

  headers2.addCopy(LowerCaseString("foo"), "bar");
  EXPECT_FALSE(headers1 == headers2);
}

TEST_P(HeaderMapImplTest, LargeCharInHeader) {
  TestRequestHeaderMapImpl headers;
  LowerCaseString static_key("\x90hello");
  std::string ref_value("value");
  headers.addReference(static_key, ref_value);
  EXPECT_EQ("value", headers.get(static_key)[0]->value().getStringView());
}

TEST_P(HeaderMapImplTest, Iterate) {
  TestRequestHeaderMapImpl headers;
  headers.addCopy(LowerCaseString("hello"), "world");
  headers.addCopy(LowerCaseString("foo"), "xxx");
  headers.addCopy(LowerCaseString("world"), "hello");
  LowerCaseString foo_key("foo");
  headers.setReferenceKey(foo_key, "bar"); // set moves key to end

  HeaderAndValueCb cb;

  InSequence seq;
  EXPECT_CALL(cb, Call("hello", "world"));
  EXPECT_CALL(cb, Call("world", "hello"));
  EXPECT_CALL(cb, Call("foo", "bar"));
  headers.iterate(cb.asIterateCb());
}

TEST_P(HeaderMapImplTest, IterateReverse) {
  TestRequestHeaderMapImpl headers;
  headers.addCopy(LowerCaseString("hello"), "world");
  headers.addCopy(LowerCaseString("foo"), "bar");
  LowerCaseString world_key("world");
  headers.setReferenceKey(world_key, "hello");

  HeaderAndValueCb cb;

  InSequence seq;
  EXPECT_CALL(cb, Call("world", "hello"));
  EXPECT_CALL(cb, Call("foo", "bar"));
  // no "hello"
  headers.iterateReverse([&cb](const Http::HeaderEntry& header) -> HeaderMap::Iterate {
    cb.Call(std::string(header.key().getStringView()), std::string(header.value().getStringView()));
    if (header.key().getStringView() != "foo") {
      return HeaderMap::Iterate::Continue;
    } else {
      return HeaderMap::Iterate::Break;
    }
  });
}

TEST_P(HeaderMapImplTest, Get) {
  {
    auto headers = TestRequestHeaderMapImpl({{Headers::get().Path.get(), "/"}, {"hello", "world"}});
    EXPECT_EQ("/", headers.get(LowerCaseString(":path"))[0]->value().getStringView());
    EXPECT_EQ("world", headers.get(LowerCaseString("hello"))[0]->value().getStringView());
    EXPECT_TRUE(headers.get(LowerCaseString("foo")).empty());
  }

  {
    auto headers = TestRequestHeaderMapImpl({{Headers::get().Path.get(), "/"}, {"hello", "world"}});
    // There is not HeaderMap method to set a header and copy both the key and value.
    const LowerCaseString path(":path");
    headers.setReferenceKey(path, "/new_path");
    EXPECT_EQ("/new_path", headers.get(LowerCaseString(":path"))[0]->value().getStringView());
    const LowerCaseString foo("hello");
    headers.setReferenceKey(foo, "world2");
    EXPECT_EQ("world2", headers.get(foo)[0]->value().getStringView());
    EXPECT_TRUE(headers.get(LowerCaseString("foo")).empty());
  }
}

TEST_P(HeaderMapImplTest, CreateHeaderMapFromIterator) {
  std::vector<std::pair<LowerCaseString, std::string>> iter_headers{
      {LowerCaseString(Headers::get().Path), "/"}, {LowerCaseString("hello"), "world"}};
  auto headers = createHeaderMap<RequestHeaderMapImpl>(iter_headers.cbegin(), iter_headers.cend());
  EXPECT_EQ("/", headers->get(LowerCaseString(":path"))[0]->value().getStringView());
  EXPECT_EQ("world", headers->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_TRUE(headers->get(LowerCaseString("foo")).empty());
}

TEST_P(HeaderMapImplTest, TestHeaderList) {
  std::array<std::string, 2> keys{Headers::get().Path.get(), "hello"};
  std::array<std::string, 2> values{"/", "world"};

  auto headers = TestRequestHeaderMapImpl({{keys[0], values[0]}, {keys[1], values[1]}});
  HeaderListView header_list(headers);
  auto to_string_views =
      [](const HeaderListView::HeaderStringRefs& strs) -> std::vector<absl::string_view> {
    std::vector<absl::string_view> str_views(strs.size());
    std::transform(strs.begin(), strs.end(), str_views.begin(),
                   [](auto value) -> absl::string_view { return value.get().getStringView(); });
    return str_views;
  };

  EXPECT_THAT(to_string_views(header_list.keys()), ElementsAre(":path", "hello"));
  EXPECT_THAT(to_string_views(header_list.values()), ElementsAre("/", "world"));
}

TEST_P(HeaderMapImplTest, TestAppendHeader) {
  // Test appending to a string with a value.
  {
    TestRequestHeaderMapImpl headers;
    LowerCaseString foo("key1");
    headers.addCopy(foo, "some;");
    headers.appendCopy(foo, "test");
    EXPECT_EQ(headers.get(foo)[0]->value().getStringView(), "some;,test");
  }

  // Test appending to an empty string.
  {
    TestRequestHeaderMapImpl headers;
    LowerCaseString key2("key2");
    headers.appendCopy(key2, "my tag data");
    EXPECT_EQ(headers.get(key2)[0]->value().getStringView(), "my tag data");
  }

  // Test empty data case.
  {
    TestRequestHeaderMapImpl headers;
    LowerCaseString key3("key3");
    headers.addCopy(key3, "empty");
    headers.appendCopy(key3, "");
    EXPECT_EQ(headers.get(key3)[0]->value().getStringView(), "empty");
  }
  // Regression test for appending to an empty string with a short string, then
  // setting integer.
  {
    TestRequestHeaderMapImpl headers;
    const std::string empty;
    headers.setPath(empty);
    // Append with default delimiter.
    headers.appendPath(" ", ",");
    headers.setPath("0");
    EXPECT_EQ("0", headers.getPathValue());
    EXPECT_EQ(1U, headers.Path()->value().size());
  }
  // Test append for inline headers using this method and append##name.
  {
    TestRequestHeaderMapImpl headers;
    headers.addCopy(Headers::get().Via, "1.0 fred");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred");
    headers.appendCopy(Headers::get().Via, "1.1 p.example.net");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred,1.1 p.example.net");
    headers.appendVia("1.1 new.example.net", ",");
    EXPECT_EQ(headers.getViaValue(), "1.0 fred,1.1 p.example.net,1.1 new.example.net");
  }
}

TEST(TestHeaderMapImplDeathTest, TestHeaderLengthChecks) {
  HeaderString value;
  value.setCopy("some;");
  EXPECT_DEATH(value.append(nullptr, std::numeric_limits<uint32_t>::max()),
               "Trying to allocate overly large headers.");

  std::string source("hello");
  HeaderString reference;
  reference.setReference(source);
  EXPECT_DEATH(reference.append(nullptr, std::numeric_limits<uint32_t>::max()),
               "Trying to allocate overly large headers.");
}

TEST_P(HeaderMapImplTest, PseudoHeaderOrder) {
  HeaderAndValueCb cb;

  {
    LowerCaseString foo("hello");
    Http::TestRequestHeaderMapImpl headers{};
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

    headers.iterate(cb.asIterateCb());

    // Removal of the header before which pseudo-headers are inserted
    EXPECT_EQ(1UL, headers.remove(foo));
    EXPECT_EQ(2UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(cb.asIterateCb());

    // Next pseudo-header goes after other pseudo-headers, but before normal headers
    headers.setReferenceKey(Headers::get().Path, "/test");
    EXPECT_EQ(3UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));
    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(cb.asIterateCb());

    // Removing the last normal header
    EXPECT_EQ(1UL, headers.remove(Headers::get().ContentType));
    EXPECT_EQ(2UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));

    headers.iterate(cb.asIterateCb());

    // Adding a new pseudo-header after removing the last normal header
    headers.setReferenceKey(Headers::get().Host, "host");
    EXPECT_EQ(3UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));
    EXPECT_CALL(cb, Call(":authority", "host"));

    headers.iterate(cb.asIterateCb());

    // Adding the first normal header
    headers.setReferenceKey(Headers::get().ContentType, "text/html");
    EXPECT_EQ(4UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":method", "PUT"));
    EXPECT_CALL(cb, Call(":path", "/test"));
    EXPECT_CALL(cb, Call(":authority", "host"));
    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(cb.asIterateCb());

    // Removing all pseudo-headers
    EXPECT_EQ(1UL, headers.remove(Headers::get().Path));
    EXPECT_EQ(1UL, headers.remove(Headers::get().Method));
    EXPECT_EQ(1UL, headers.remove(Headers::get().Host));
    EXPECT_EQ(1UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call("content-type", "text/html"));

    headers.iterate(cb.asIterateCb());

    // Removing all headers
    EXPECT_EQ(1UL, headers.remove(Headers::get().ContentType));
    EXPECT_EQ(0UL, headers.size());
    EXPECT_TRUE(headers.empty());

    // Adding a lone pseudo-header
    headers.setReferenceKey(Headers::get().Status, "200");
    EXPECT_EQ(1UL, headers.size());
    EXPECT_FALSE(headers.empty());

    EXPECT_CALL(cb, Call(":status", "200"));

    headers.iterate(cb.asIterateCb());
  }

  // Starting with a normal header
  {
    auto headers = TestRequestHeaderMapImpl({{Headers::get().ContentType.get(), "text/plain"},
                                             {Headers::get().Method.get(), "GET"},
                                             {Headers::get().Path.get(), "/"},
                                             {"hello", "world"},
                                             {Headers::get().Host.get(), "host"}});

    InSequence seq;
    EXPECT_CALL(cb, Call(":method", "GET"));
    EXPECT_CALL(cb, Call(":path", "/"));
    EXPECT_CALL(cb, Call(":authority", "host"));
    EXPECT_CALL(cb, Call("content-type", "text/plain"));
    EXPECT_CALL(cb, Call("hello", "world"));

    headers.iterate(cb.asIterateCb());
  }

  // Starting with a pseudo-header
  {
    auto headers = TestRequestHeaderMapImpl({{Headers::get().Path.get(), "/"},
                                             {Headers::get().ContentType.get(), "text/plain"},
                                             {Headers::get().Method.get(), "GET"},
                                             {"hello", "world"},
                                             {Headers::get().Host.get(), "host"}});

    InSequence seq;
    EXPECT_CALL(cb, Call(":path", "/"));
    EXPECT_CALL(cb, Call(":method", "GET"));
    EXPECT_CALL(cb, Call(":authority", "host"));
    EXPECT_CALL(cb, Call("content-type", "text/plain"));
    EXPECT_CALL(cb, Call("hello", "world"));

    headers.iterate(cb.asIterateCb());
  }
}

// Validate that TestRequestHeaderMapImpl copy construction and assignment works. This is a
// regression for where we were missing a valid copy constructor and had the
// default (dangerous) move semantics takeover.
TEST_P(HeaderMapImplTest, TestRequestHeaderMapImplCopy) {
  TestRequestHeaderMapImpl foo;
  foo.addCopy(LowerCaseString("foo"), "bar");
  auto headers = std::make_unique<TestRequestHeaderMapImpl>(foo);
  EXPECT_EQ("bar", headers->get(LowerCaseString("foo"))[0]->value().getStringView());
  TestRequestHeaderMapImpl baz{{"foo", "baz"}};
  baz = *headers;
  EXPECT_EQ("bar", baz.get(LowerCaseString("foo"))[0]->value().getStringView());
  const TestRequestHeaderMapImpl& baz2 = baz;
  baz = baz2;
  EXPECT_EQ("bar", baz.get(LowerCaseString("foo"))[0]->value().getStringView());
}

// Make sure 'host' -> ':authority' auto translation only occurs for request headers.
TEST_P(HeaderMapImplTest, HostHeader) {
  TestRequestHeaderMapImpl request_headers{{"host", "foo"}};
  EXPECT_EQ(request_headers.size(), 1);
  EXPECT_EQ(request_headers.get_(":authority"), "foo");

  TestRequestTrailerMapImpl request_trailers{{"host", "foo"}};
  EXPECT_EQ(request_trailers.size(), 1);
  EXPECT_EQ(request_trailers.get_("host"), "foo");

  TestResponseHeaderMapImpl response_headers{{"host", "foo"}};
  EXPECT_EQ(response_headers.size(), 1);
  EXPECT_EQ(response_headers.get_("host"), "foo");

  TestResponseTrailerMapImpl response_trailers{{"host", "foo"}};
  EXPECT_EQ(response_trailers.size(), 1);
  EXPECT_EQ(response_trailers.get_("host"), "foo");
}

TEST_P(HeaderMapImplTest, TestInlineHeaderAdd) {
  TestRequestHeaderMapImpl foo;
  foo.addCopy(LowerCaseString(":path"), "GET");
  EXPECT_EQ(foo.size(), 1);
  EXPECT_TRUE(foo.Path() != nullptr);
}

TEST_P(HeaderMapImplTest, ClearHeaderMap) {
  TestRequestHeaderMapImpl headers;
  LowerCaseString static_key("hello");
  std::string ref_value("value");

  // Add random header and then clear.
  headers.addReference(static_key, ref_value);
  EXPECT_EQ("value", headers.get(static_key)[0]->value().getStringView());
  EXPECT_TRUE(headers.get(static_key)[0]->value().isReference());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  headers.clear();
  EXPECT_TRUE(headers.get(static_key).empty());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_EQ(headers.byteSize(), 0);
  EXPECT_TRUE(headers.empty());

  // Add inline and clear.
  headers.setContentLength(5);
  EXPECT_EQ("5", headers.getContentLengthValue());
  EXPECT_EQ(1UL, headers.size());
  EXPECT_FALSE(headers.empty());
  headers.clear();
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_EQ(headers.byteSize(), 0);
  EXPECT_TRUE(headers.empty());

  // Add mixture of headers.
  headers.addReference(static_key, ref_value);
  headers.setContentLength(5);
  headers.addCopy(static_key, "new_value");
  EXPECT_EQ(3UL, headers.size());
  EXPECT_FALSE(headers.empty());
  headers.clear();
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ(0UL, headers.size());
  EXPECT_EQ(headers.byteSize(), 0);
  EXPECT_TRUE(headers.empty());
}

// Validates byte size is properly accounted for in different inline header setting scenarios.
TEST_P(HeaderMapImplTest, InlineHeaderByteSize) {
  {
    TestRequestHeaderMapImpl headers;
    std::string foo = "foo";
    headers.setHost(foo);
    EXPECT_EQ(headers.byteSize(), 13);
  }
  {
    // Overwrite an inline headers with set.
    TestRequestHeaderMapImpl headers;
    std::string foo = "foo";
    headers.setHost(foo);
    std::string big_foo = "big_foo";
    headers.setHost(big_foo);
    EXPECT_EQ(headers.byteSize(), 17);
  }
  {
    // Overwrite an inline headers with setReference and clear.
    TestRequestHeaderMapImpl headers;
    std::string foo = "foo";
    headers.setHost(foo);
    EXPECT_EQ(headers.byteSize(), 13);
    std::string big_foo = "big_foo";
    headers.setReferenceHost(big_foo);
    EXPECT_EQ(headers.byteSize(), 17);
    EXPECT_EQ(1UL, headers.removeHost());
    EXPECT_EQ(headers.byteSize(), 0);
  }
  {
    // Overwrite an inline headers with set integer value.
    TestResponseHeaderMapImpl headers;
    uint64_t status = 200;
    headers.setStatus(status);
    EXPECT_EQ(headers.byteSize(), 10);
    uint64_t newStatus = 500;
    headers.setStatus(newStatus);
    EXPECT_EQ(headers.byteSize(), 10);
    EXPECT_EQ(1UL, headers.removeStatus());
    EXPECT_EQ(headers.byteSize(), 0);
  }
  {
    // Set an inline header, remove, and rewrite.
    TestResponseHeaderMapImpl headers;
    uint64_t status = 200;
    headers.setStatus(status);
    EXPECT_EQ(headers.byteSize(), 10);
    EXPECT_EQ(1UL, headers.removeStatus());
    EXPECT_EQ(headers.byteSize(), 0);
    uint64_t newStatus = 500;
    headers.setStatus(newStatus);
    EXPECT_EQ(headers.byteSize(), 10);
  }
}

TEST_P(HeaderMapImplTest, ValidHeaderString) {
  EXPECT_TRUE(validHeaderString("abc"));
  EXPECT_FALSE(validHeaderString(absl::string_view("a\000bc", 4)));
  EXPECT_FALSE(validHeaderString("abc\n"));
}

} // namespace Http
} // namespace Envoy
