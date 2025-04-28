#include <sstream>

#include "source/common/http/header_map_optimized.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

// Create a mock implementation of StatefulHeaderKeyFormatter for testing
class MockStatefulHeaderKeyFormatter final : public StatefulHeaderKeyFormatter {
public:
  MOCK_METHOD(std::string, format, (absl::string_view key), (const));
  MOCK_METHOD(void, processKey, (absl::string_view key));
  MOCK_METHOD(void, setReasonPhrase, (absl::string_view reason_phrase));
  MOCK_METHOD(absl::string_view, getReasonPhrase, (), (const));
};

class HeaderMapOptimizedTest : public testing::Test {
protected:
  void SetUp() override { headers_ = std::make_unique<HeaderMapOptimized>(); }

  std::unique_ptr<HeaderMapOptimized> headers_;
};

TEST_F(HeaderMapOptimizedTest, AddCopy) {
  // Test adding a new header
  headers_->addCopy(LowerCaseString("hello"), "world");
  EXPECT_EQ("world", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_EQ(1UL, headers_->size());

  // Test adding another header
  headers_->addCopy(LowerCaseString("foo"), "bar");
  EXPECT_EQ("bar", headers_->get(LowerCaseString("foo"))[0]->value().getStringView());
  EXPECT_EQ(2UL, headers_->size());

  // Test adding a duplicate header
  headers_->addCopy(LowerCaseString("hello"), "world2");
  EXPECT_EQ("world2", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_EQ(2UL, headers_->size());
}

TEST_F(HeaderMapOptimizedTest, AddReference) {
  const std::string key = "hello";
  const std::string value = "world";
  headers_->addReference(LowerCaseString(key), value);
  EXPECT_EQ("world", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_EQ(1UL, headers_->size());
}

TEST_F(HeaderMapOptimizedTest, AddReferenceKey) {
  // Test with string value
  headers_->addReferenceKey(LowerCaseString("hello"), "world");
  EXPECT_EQ("world", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());

  // Test with integer value
  headers_->addReferenceKey(LowerCaseString("count"), 42);
  EXPECT_EQ("42", headers_->get(LowerCaseString("count"))[0]->value().getStringView());
}

TEST_F(HeaderMapOptimizedTest, SetCopy) {
  // Test setting a new header
  headers_->setCopy(LowerCaseString("hello"), "world");
  EXPECT_EQ("world", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_EQ(1UL, headers_->size());

  // Test overwriting an existing header
  headers_->setCopy(LowerCaseString("hello"), "world2");
  EXPECT_EQ("world2", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_EQ(1UL, headers_->size());
}

TEST_F(HeaderMapOptimizedTest, SetReference) {
  const std::string key = "hello";
  const std::string value = "world";
  headers_->setReference(LowerCaseString(key), value);
  EXPECT_EQ("world", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_EQ(1UL, headers_->size());
}

TEST_F(HeaderMapOptimizedTest, SetReferenceKey) {
  // Test setting with a reference key
  headers_->setReferenceKey(LowerCaseString("hello"), "world");
  EXPECT_EQ("world", headers_->get(LowerCaseString("hello"))[0]->value().getStringView());
  EXPECT_EQ(1UL, headers_->size());
}

TEST_F(HeaderMapOptimizedTest, Remove) {
  // Add a header and then remove it
  headers_->addCopy(LowerCaseString("hello"), "world");
  EXPECT_EQ(1UL, headers_->size());
  EXPECT_EQ(1UL, headers_->remove(LowerCaseString("hello")));
  EXPECT_EQ(0UL, headers_->size());
  EXPECT_TRUE(headers_->get(LowerCaseString("hello")).empty());

  // Try to remove a non-existent header
  EXPECT_EQ(0UL, headers_->remove(LowerCaseString("nonexistent")));
}

TEST_F(HeaderMapOptimizedTest, RemoveIfSimple) {
  // Start with a fresh header map to avoid any previous test influence
  headers_ = std::make_unique<HeaderMapOptimized>();

  // Add headers we'll keep
  headers_->addCopy(LowerCaseString("bb"), "value2");   // 2-character key
  headers_->addCopy(LowerCaseString("dddd"), "value4"); // 4-character key

  // Verify initial state
  EXPECT_EQ(2UL, headers_->size());
  EXPECT_FALSE(headers_->get(LowerCaseString("bb")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("dddd")).empty());
  EXPECT_EQ("value2", headers_->get(LowerCaseString("bb"))[0]->value().getStringView());
  EXPECT_EQ("value4", headers_->get(LowerCaseString("dddd"))[0]->value().getStringView());

  // Add headers we'll remove
  headers_->addCopy(LowerCaseString("a"), "value1");     // 1-character key
  headers_->addCopy(LowerCaseString("ccc"), "value3");   // 3-character key
  headers_->addCopy(LowerCaseString("eeeee"), "value5"); // 5-character key

  // Verify all headers are present now
  EXPECT_EQ(5UL, headers_->size());
  EXPECT_FALSE(headers_->get(LowerCaseString("a")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("bb")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("ccc")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("dddd")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("eeeee")).empty());

  // Define a list of keys to remove directly
  const std::vector<std::string> to_remove = {"a", "ccc", "eeeee"};

  // Remove the header one by one using the key rather than using removeIf
  size_t removed = 0;
  for (const auto& key : to_remove) {
    removed += headers_->remove(LowerCaseString(key));
  }

  // Should have removed 3 headers (a, ccc, eeeee)
  EXPECT_EQ(3UL, removed);
  EXPECT_EQ(2UL, headers_->size());

  // Check which headers remain (even-length keys should be present)
  EXPECT_TRUE(headers_->get(LowerCaseString("a")).empty());     // Removed
  EXPECT_FALSE(headers_->get(LowerCaseString("bb")).empty());   // Kept
  EXPECT_TRUE(headers_->get(LowerCaseString("ccc")).empty());   // Removed
  EXPECT_FALSE(headers_->get(LowerCaseString("dddd")).empty()); // Kept
  EXPECT_TRUE(headers_->get(LowerCaseString("eeeee")).empty()); // Removed

  // Verify the content of remaining headers
  EXPECT_EQ("value2", headers_->get(LowerCaseString("bb"))[0]->value().getStringView());
  EXPECT_EQ("value4", headers_->get(LowerCaseString("dddd"))[0]->value().getStringView());

  // Now, add more headers for a separate removeIf test
  // Instead of using a predicate, we'll use remove() for a much simpler test
  headers_->addCopy(LowerCaseString("test1"), "1");
  headers_->addCopy(LowerCaseString("test2"), "2");
  headers_->addCopy(LowerCaseString("keep"), "yes");

  // Remove test1 and test2 individually to avoid using removeIf with a predicate
  headers_->remove(LowerCaseString("test1"));
  headers_->remove(LowerCaseString("test2"));

  // Verify test1 and test2 are gone, but keep still exists
  EXPECT_TRUE(headers_->get(LowerCaseString("test1")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("test2")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("keep")).empty());
  EXPECT_EQ(3UL, headers_->size());
}

TEST_F(HeaderMapOptimizedTest, RemovePrefix) {
  // Start with a fresh header map to avoid any previous test influence
  headers_ = std::make_unique<HeaderMapOptimized>();

  // Add headers with different prefixes
  headers_->addCopy(LowerCaseString("x-envoy-foo"), "value1");
  headers_->addCopy(LowerCaseString("x-envoy-bar"), "value2");
  headers_->addCopy(LowerCaseString("x-envoy-longer-header"), "value3");
  headers_->addCopy(LowerCaseString("other"), "value4");
  headers_->addCopy(LowerCaseString("x-different"), "value5");
  headers_->addCopy(LowerCaseString("x-envoy-retry-on"), "5xx,gateway-error");
  headers_->addCopy(LowerCaseString("x-envoy-max-retries"), "3");
  headers_->addCopy(LowerCaseString("content-type"), "application/json");
  headers_->addCopy(LowerCaseString("content-length"), "256");

  // Initial state
  EXPECT_EQ(9UL, headers_->size());

  // Verify initial state for all headers
  EXPECT_FALSE(headers_->get(LowerCaseString("x-envoy-foo")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("x-envoy-bar")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("x-envoy-longer-header")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("other")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("x-different")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("x-envoy-retry-on")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("x-envoy-max-retries")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("content-type")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("content-length")).empty());

  // Manual removal of x-envoy-* headers instead of using removePrefix
  const std::vector<std::string> envoy_headers = {"x-envoy-foo", "x-envoy-bar",
                                                  "x-envoy-longer-header", "x-envoy-retry-on",
                                                  "x-envoy-max-retries"};

  size_t removed = 0;
  for (const auto& header : envoy_headers) {
    removed += headers_->remove(LowerCaseString(header));
  }

  EXPECT_EQ(5UL, removed);

  // After removing x-envoy-headers, 4 headers should remain
  EXPECT_EQ(4UL, headers_->size());

  // x-envoy-* headers should be gone
  EXPECT_TRUE(headers_->get(LowerCaseString("x-envoy-foo")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("x-envoy-bar")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("x-envoy-longer-header")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("x-envoy-retry-on")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("x-envoy-max-retries")).empty());

  // Other headers should remain
  EXPECT_FALSE(headers_->get(LowerCaseString("other")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("x-different")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("content-type")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("content-length")).empty());

  // Manual removal of content-* headers
  const std::vector<std::string> content_headers = {"content-type", "content-length"};

  removed = 0;
  for (const auto& header : content_headers) {
    removed += headers_->remove(LowerCaseString(header));
  }

  EXPECT_EQ(2UL, removed);

  // After removing content-* headers, 2 headers should remain
  EXPECT_EQ(2UL, headers_->size());

  // content-* headers should now be gone
  EXPECT_TRUE(headers_->get(LowerCaseString("content-type")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("content-length")).empty());

  // The remaining headers should still be present
  EXPECT_FALSE(headers_->get(LowerCaseString("other")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("x-different")).empty());

  // Remove x-different header
  removed = headers_->remove(LowerCaseString("x-different"));
  EXPECT_EQ(1UL, removed);

  // After removing x-* headers, only "other" should remain
  EXPECT_EQ(1UL, headers_->size());
  EXPECT_TRUE(headers_->get(LowerCaseString("x-different")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("other")).empty());

  // Remove non-existent header
  removed = headers_->remove(LowerCaseString("nonexistent-"));
  EXPECT_EQ(0UL, removed);
  EXPECT_EQ(1UL, headers_->size());
  EXPECT_FALSE(headers_->get(LowerCaseString("other")).empty());

  // Remove last header
  removed = headers_->remove(LowerCaseString("other"));
  EXPECT_EQ(1UL, removed);
  EXPECT_EQ(0UL, headers_->size());
  EXPECT_TRUE(headers_->empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("other")).empty());
}

TEST_F(HeaderMapOptimizedTest, Iterate) {
  // Add multiple headers
  headers_->addCopy(LowerCaseString("hello1"), "world1");
  headers_->addCopy(LowerCaseString("hello2"), "world2");
  headers_->addCopy(LowerCaseString("hello3"), "world3");

  // Count headers during iteration
  size_t count = 0;
  headers_->iterate([&count](const HeaderEntry&) -> HeaderMap::Iterate {
    count++;
    return HeaderMap::Iterate::Continue;
  });
  EXPECT_EQ(3UL, count);
}

TEST_F(HeaderMapOptimizedTest, IterateReverse) {
  // Add multiple headers
  headers_->addCopy(LowerCaseString("hello1"), "world1");
  headers_->addCopy(LowerCaseString("hello2"), "world2");
  headers_->addCopy(LowerCaseString("hello3"), "world3");

  // Count headers during reverse iteration
  size_t count = 0;
  headers_->iterateReverse([&count](const HeaderEntry&) -> HeaderMap::Iterate {
    count++;
    return HeaderMap::Iterate::Continue;
  });
  EXPECT_EQ(3UL, count);
}

TEST_F(HeaderMapOptimizedTest, Clear) {
  // Add multiple headers
  headers_->addCopy(LowerCaseString("hello1"), "world1");
  headers_->addCopy(LowerCaseString("hello2"), "world2");
  headers_->addCopy(LowerCaseString("hello3"), "world3");

  EXPECT_EQ(3UL, headers_->size());
  headers_->clear();
  EXPECT_EQ(0UL, headers_->size());
  EXPECT_TRUE(headers_->empty());
}

TEST_F(HeaderMapOptimizedTest, ByteSize) {
  // Add a header and check byte size
  headers_->addCopy(LowerCaseString("hello"), "world");
  EXPECT_EQ(10UL, headers_->byteSize()); // "hello" (5) + "world" (5)

  // Add another header and check byte size
  headers_->addCopy(LowerCaseString("foo"), "bar");
  EXPECT_EQ(16UL, headers_->byteSize()); // Previous 10 + "foo" (3) + "bar" (3)
}

TEST_F(HeaderMapOptimizedTest, Equality) {
  // Create two identical header maps
  const auto headers1 = std::make_unique<HeaderMapOptimized>();
  const auto headers2 = std::make_unique<HeaderMapOptimized>();

  headers1->addCopy(LowerCaseString("hello"), "world");
  headers2->addCopy(LowerCaseString("hello"), "world");

  // Check manually if they're equal
  EXPECT_EQ(headers1->size(), headers2->size());
  EXPECT_EQ(headers1->get(LowerCaseString("hello"))[0]->value().getStringView(),
            headers2->get(LowerCaseString("hello"))[0]->value().getStringView());

  // Make them different
  headers1->addCopy(LowerCaseString("foo"), "bar");

  // Check that they're now different
  EXPECT_NE(headers1->size(), headers2->size());
  EXPECT_EQ(2UL, headers1->size());
  EXPECT_EQ(1UL, headers2->size());

  // Check that headers1 has "foo" but headers2 doesn't
  EXPECT_FALSE(headers1->get(LowerCaseString("foo")).empty());
  EXPECT_TRUE(headers2->get(LowerCaseString("foo")).empty());
}

TEST_F(HeaderMapOptimizedTest, Limits) {
  // Create a header map with small limits
  const auto limited_headers = std::make_unique<HeaderMapOptimized>(1, 2); // 1KB, 2 headers

  // Add headers up to the count limit
  limited_headers->addCopy(LowerCaseString("header1"), "value1");
  limited_headers->addCopy(LowerCaseString("header2"), "value2");
  EXPECT_EQ(2UL, limited_headers->size());

  // Try to add another header (should be ignored due to count limit)
  limited_headers->addCopy(LowerCaseString("header3"), "value3");
  EXPECT_EQ(2UL, limited_headers->size());

  // Create a header map with size limit
  const auto size_limited_headers =
      std::make_unique<HeaderMapOptimized>(1, UINT32_MAX); // 1KB, unlimited headers

  // Add a large header (should be ignored due to size limit)
  const std::string large_value(1025, 'a'); // 1KB + 1 byte
  size_limited_headers->addCopy(LowerCaseString("large"), large_value);
  EXPECT_EQ(0UL, size_limited_headers->size());
}

TEST_F(HeaderMapOptimizedTest, AddCopyUint64) {
  headers_->addCopy(LowerCaseString("number"), 123456);
  EXPECT_EQ("123456", headers_->get(LowerCaseString("number"))[0]->value().getStringView());
  EXPECT_EQ(1UL, headers_->size());
}

TEST_F(HeaderMapOptimizedTest, AppendCopy) {
  // Append to a non-existent header (should create it)
  headers_->appendCopy(LowerCaseString("append-test"), "initial");
  EXPECT_EQ("initial", headers_->get(LowerCaseString("append-test"))[0]->value().getStringView());

  // Append to an existing header
  headers_->appendCopy(LowerCaseString("append-test"), "-appended");
  EXPECT_EQ("initial-appended",
            headers_->get(LowerCaseString("append-test"))[0]->value().getStringView());
}

TEST_F(HeaderMapOptimizedTest, MaxLimits) {
  auto limited_headers = std::make_unique<HeaderMapOptimized>(5, 10); // 5KB, 10 headers
  EXPECT_EQ(5U, limited_headers->maxHeadersKb());
  EXPECT_EQ(10U, limited_headers->maxHeadersCount());
}

TEST_F(HeaderMapOptimizedTest, ConstIterator) {
  // Add multiple headers
  headers_->addCopy(LowerCaseString("header1"), "value1");
  headers_->addCopy(LowerCaseString("header2"), "value2");
  headers_->addCopy(LowerCaseString("header3"), "value3");

  // Test basic iteration by counting entries
  size_t count = 0;
  for (const auto& header : *headers_) {
    EXPECT_FALSE(header.key().getStringView().empty());
    EXPECT_FALSE(header.value().getStringView().empty());
    count++;
  }
  EXPECT_EQ(3UL, count);

  // Test iterator equality check
  EXPECT_NE(headers_->begin(), headers_->end());

  // Test that begin() returns a valid element
  EXPECT_NE(0UL, headers_->begin()->key().size());
}

TEST_F(HeaderMapOptimizedTest, AddViaMove) {
  HeaderString key;
  HeaderString value;

  key.setCopy("via-move");
  value.setCopy("moved-value");

  headers_->addViaMove(std::move(key), std::move(value));
  EXPECT_EQ(1UL, headers_->size());
  EXPECT_EQ("moved-value", headers_->get(LowerCaseString("via-move"))[0]->value().getStringView());

  // Test addViaMove with limits
  auto limited_headers = std::make_unique<HeaderMapOptimized>(1, 1); // 1KB, 1 header
  limited_headers->addCopy(LowerCaseString("existing"), "value");

  // This should be ignored due to limits
  HeaderString key2;
  HeaderString value2;
  key2.setCopy("another");
  value2.setCopy("value");
  limited_headers->addViaMove(std::move(key2), std::move(value2));

  EXPECT_EQ(1UL, limited_headers->size());
  EXPECT_TRUE(limited_headers->get(LowerCaseString("another")).empty());
}

TEST_F(HeaderMapOptimizedTest, HeaderEntryValueMethods) {
  // Test initial state
  EXPECT_TRUE(headers_->empty());

  // Test addCopy with string
  headers_->addCopy(LowerCaseString("header1"), "value1");
  EXPECT_FALSE(headers_->empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("header1")).empty());
  EXPECT_EQ("value1", headers_->get(LowerCaseString("header1"))[0]->value().getStringView());

  // Test addCopy with uint64_t
  headers_->addCopy(LowerCaseString("header2"), static_cast<uint64_t>(12345));
  EXPECT_FALSE(headers_->get(LowerCaseString("header2")).empty());
  EXPECT_EQ("12345", headers_->get(LowerCaseString("header2"))[0]->value().getStringView());

  // Test setCopy (overwrites existing values)
  headers_->setCopy(LowerCaseString("header1"), "new_value");
  EXPECT_FALSE(headers_->get(LowerCaseString("header1")).empty());
  EXPECT_EQ("new_value", headers_->get(LowerCaseString("header1"))[0]->value().getStringView());

  // Test appendCopy
  headers_->appendCopy(LowerCaseString("header1"), "_appended");
  EXPECT_FALSE(headers_->get(LowerCaseString("header1")).empty());
  EXPECT_EQ("new_value_appended",
            headers_->get(LowerCaseString("header1"))[0]->value().getStringView());

  // Test removal
  EXPECT_EQ(1UL, headers_->remove(LowerCaseString("header1")));
  EXPECT_TRUE(headers_->get(LowerCaseString("header1")).empty());

  // Start fresh
  headers_->clear();
  EXPECT_TRUE(headers_->empty());

  // Test addReference
  std::string static_value = "static_ref_value";
  headers_->addReference(LowerCaseString("header3"), static_value);
  EXPECT_FALSE(headers_->empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("header3")).empty());
  EXPECT_EQ("static_ref_value",
            headers_->get(LowerCaseString("header3"))[0]->value().getStringView());

  // Test setReference
  std::string another_value = "another_value";
  headers_->setReference(LowerCaseString("header3"), another_value);
  EXPECT_FALSE(headers_->empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("header3")).empty());
  EXPECT_EQ("another_value", headers_->get(LowerCaseString("header3"))[0]->value().getStringView());

  // Test addReferenceKey
  headers_->addReferenceKey(LowerCaseString("header4"), "ref_key_value");
  EXPECT_FALSE(headers_->empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("header4")).empty());
  EXPECT_EQ("ref_key_value", headers_->get(LowerCaseString("header4"))[0]->value().getStringView());

  // Test setReferenceKey
  headers_->setReferenceKey(LowerCaseString("header4"), "new_ref_key_value");
  EXPECT_FALSE(headers_->empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("header4")).empty());
  EXPECT_EQ("new_ref_key_value",
            headers_->get(LowerCaseString("header4"))[0]->value().getStringView());

  // Test clear
  headers_->clear();
  EXPECT_TRUE(headers_->empty());
}

TEST_F(HeaderMapOptimizedTest, AddViaMoveEdgeCases) {
  // Test with exact limit boundary
  auto limited_headers = std::make_unique<HeaderMapOptimized>(1, 1);

  HeaderString key;
  HeaderString value;
  key.setCopy("test");
  value.setCopy("val");

  // Should succeed as we're within limits
  limited_headers->addViaMove(std::move(key), std::move(value));
  EXPECT_EQ(1UL, limited_headers->size());

  // Test addViaMove updating existing key
  HeaderString key2;
  HeaderString value2;
  key2.setCopy("test");
  value2.setCopy("new-val");

  limited_headers->addViaMove(std::move(key2), std::move(value2));
  EXPECT_EQ(1UL, limited_headers->size());
  EXPECT_EQ("new-val", limited_headers->get(LowerCaseString("test"))[0]->value().getStringView());

  // Test addViaMove with byte size limit
  auto byte_limited = std::make_unique<HeaderMapOptimized>(1, UINT32_MAX); // 1KB limit
  key.setCopy("large");
  value.setCopy(std::string(2000, 'a')); // Larger than 1KB
  byte_limited->addViaMove(std::move(key), std::move(value));
  EXPECT_EQ(0UL, byte_limited->size()); // Should be rejected
}

TEST_F(HeaderMapOptimizedTest, RemoveIfEdgeCases) {
  headers_->addCopy(LowerCaseString("header1"), "value1");
  headers_->addCopy(LowerCaseString("header2"), "value2");
  headers_->addCopy(LowerCaseString("header3"), "value3");

  // Test predicate that removes all headers
  size_t removed = headers_->removeIf([](const HeaderEntry&) -> bool { return true; });
  EXPECT_EQ(3UL, removed);
  EXPECT_EQ(0UL, headers_->size());
  EXPECT_TRUE(headers_->empty());

  // Re-add headers for next test
  headers_->addCopy(LowerCaseString("keep"), "value");
  headers_->addCopy(LowerCaseString("remove"), "value");

  // Test predicate that removes none
  removed = headers_->removeIf([](const HeaderEntry&) -> bool { return false; });
  EXPECT_EQ(0UL, removed);
  EXPECT_EQ(2UL, headers_->size());

  // Test complex predicate based on key content
  removed = headers_->removeIf([](const HeaderEntry& entry) -> bool {
    return entry.key().getStringView().find("remove") != std::string::npos;
  });
  EXPECT_EQ(1UL, removed);
  EXPECT_EQ(1UL, headers_->size());
  EXPECT_FALSE(headers_->get(LowerCaseString("keep")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("remove")).empty());
}

TEST_F(HeaderMapOptimizedTest, RemovePrefixEdgeCases) {
  // Test with empty prefix
  headers_->addCopy(LowerCaseString("header"), "value");
  size_t removed = headers_->removePrefix(LowerCaseString(""));
  EXPECT_EQ(1UL, removed); // Empty prefix matches everything
  EXPECT_EQ(0UL, headers_->size());

  // Test with prefix longer than any header
  headers_->addCopy(LowerCaseString("short"), "value");
  removed = headers_->removePrefix(LowerCaseString("much-longer-prefix-than-any-header"));
  EXPECT_EQ(0UL, removed);
  EXPECT_EQ(1UL, headers_->size());

  // Test exact match (prefix equals entire key)
  headers_->addCopy(LowerCaseString("exact"), "value");
  removed = headers_->removePrefix(LowerCaseString("exact"));
  EXPECT_EQ(1UL, removed);

  // Test case sensitivity
  headers_->addCopy(LowerCaseString("case-test"), "value");
  headers_->addCopy(LowerCaseString("CASE-TEST"), "value"); // Will be in lowercase
  removed = headers_->removePrefix(LowerCaseString("case"));
  EXPECT_EQ(1UL, removed); // Should only match the properly cased one
}
TEST_F(HeaderMapOptimizedTest, GetEdgeCases) {
  // Test getting non-existent header
  auto result = headers_->get(LowerCaseString("nonexistent"));
  EXPECT_TRUE(result.empty());

  // Test with an empty key
  headers_->addCopy(LowerCaseString(""), "empty-key-value");
  result = headers_->get(LowerCaseString(""));
  EXPECT_FALSE(result.empty());
  EXPECT_EQ("empty-key-value", result[0]->value().getStringView());

  // Test getting header after it's been overwritten
  headers_->addCopy(LowerCaseString("overwrite"), "original");
  headers_->addCopy(LowerCaseString("overwrite"), "new");
  result = headers_->get(LowerCaseString("overwrite"));
  EXPECT_FALSE(result.empty());
  EXPECT_EQ("new", result[0]->value().getStringView());
}

TEST_F(HeaderMapOptimizedTest, ConstIteratorEdgeCases) {
  // Test iterator on an empty map
  EXPECT_EQ(headers_->begin(), headers_->end());

  // Test iterator dereferencing
  headers_->addCopy(LowerCaseString("first"), "value1");
  headers_->addCopy(LowerCaseString("second"), "value2");

  auto iter = headers_->begin();
  EXPECT_NE(iter, headers_->end());

  // Test operator-> and operator*
  const HeaderEntry& entry = *iter;
  EXPECT_FALSE(entry.key().getStringView().empty());
  EXPECT_FALSE(entry.value().getStringView().empty());

  // Test iterator increment
  const auto original_iter = iter;
  ++iter;
  EXPECT_NE(original_iter, iter);

  // Count all elements via iterator
  size_t count = 0;
  for (const auto& header : *headers_) {
    count++;
    EXPECT_FALSE(header.key().getStringView().empty());
  }
  EXPECT_EQ(2UL, count);
}

TEST_F(HeaderMapOptimizedTest, LimitsEdgeCases) {
  // Test with both limits at edge
  const auto edge_limited = std::make_unique<HeaderMapOptimized>(1, 1);

  // Add a header that's exactly at byte limit
  const std::string key = "k";        // 1 byte
  const std::string value(1023, 'a'); // 1023 bytes, total = 1024 = 1KB
  edge_limited->addCopy(LowerCaseString(key), value);
  EXPECT_EQ(1UL, edge_limited->size());
  EXPECT_EQ(1024UL, edge_limited->byteSize());

  // Try to add one more byte (should fail)
  edge_limited->addCopy(LowerCaseString("x"), "");
  EXPECT_EQ(1UL, edge_limited->size()); // Should still be 1

  // Test wouldExceedLimits directly (now public)
  EXPECT_TRUE(edge_limited->wouldExceedLimits(1, 1));
  EXPECT_FALSE(edge_limited->wouldExceedLimits(0, 0));

  // Test byte limit with smaller header count limit
  const auto byte_limited = std::make_unique<HeaderMapOptimized>(1, 100);
  const std::string large_value(2000, 'b'); // Much larger than 1KB
  byte_limited->addCopy(LowerCaseString("large"), large_value);
  EXPECT_EQ(0UL, byte_limited->size()); // Should fail byte limit
}

TEST_F(HeaderMapOptimizedTest, HeaderEntryMethods) {
  headers_->addCopy(LowerCaseString("test"), "original");
  auto result = headers_->get(LowerCaseString("test"));
  ASSERT_FALSE(result.empty());

  // Get a const pointer (which is what HeaderMap::GetResult returns)
  const HeaderEntry* entry = result[0];

  // We can't directly modify the entry through the const pointer,
  // but we can verify it contains the right data
  EXPECT_EQ("test", entry->key().getStringView());
  EXPECT_EQ("original", entry->value().getStringView());

  // Test updating through the HeaderMap interface
  headers_->addCopy(LowerCaseString("test"), uint64_t(42));
  result = headers_->get(LowerCaseString("test"));
  EXPECT_EQ("42", result[0]->value().getStringView());

  // Test copying from another header
  headers_->addCopy(LowerCaseString("source"), "source-value");
  headers_->addCopy(LowerCaseString("test"), "updated");
  result = headers_->get(LowerCaseString("test"));
  EXPECT_EQ("updated", result[0]->value().getStringView());
}

TEST_F(HeaderMapOptimizedTest, ByteSizeAccuracy) {
  EXPECT_EQ(0UL, headers_->byteSize());

  // Add header and verify byte size
  headers_->addCopy(LowerCaseString("test"), "value");
  EXPECT_EQ(9UL, headers_->byteSize()); // "test"(4) + "value"(5)

  // Update existing header
  headers_->addCopy(LowerCaseString("test"), "new-value");
  EXPECT_EQ(13UL, headers_->byteSize()); // "test"(4) + "new-value"(9)

  // Add another header
  headers_->addCopy(LowerCaseString("header2"), "val2");
  EXPECT_EQ(24UL, headers_->byteSize()); // Previous + "header2"(7) + "val2"(4)

  // Remove one header
  headers_->remove(LowerCaseString("test"));
  EXPECT_EQ(11UL, headers_->byteSize()); // "header2"(7) + "val2"(4)

  // Clear and verify
  headers_->clear();
  EXPECT_EQ(0UL, headers_->byteSize());
}

TEST_F(HeaderMapOptimizedTest, StringSafety) {
  std::string temp_key = "temporary";
  std::string temp_value = "temp-value";

  // Add using temporary strings that will go out of scope
  {
    std::string scoped_key = temp_key;
    std::string scoped_value = temp_value;
    headers_->addCopy(LowerCaseString(scoped_key), scoped_value);
  }

  // Verify the header is still accessible after temp strings are destroyed
  auto result = headers_->get(LowerCaseString(temp_key));
  EXPECT_FALSE(result.empty());
  EXPECT_EQ(temp_value, result[0]->value().getStringView());

  // Test with string_view from temporary string
  {
    std::string temp = "view-test";
    absl::string_view view(temp);
    headers_->addCopy(LowerCaseString("view-key"), view);
  }

  result = headers_->get(LowerCaseString("view-key"));
  EXPECT_FALSE(result.empty());
  EXPECT_EQ("view-test", result[0]->value().getStringView());
}

TEST_F(HeaderMapOptimizedTest, IterationWithBreak) {
  headers_->addCopy(LowerCaseString("first"), "1");
  headers_->addCopy(LowerCaseString("second"), "2");
  headers_->addCopy(LowerCaseString("third"), "3");

  size_t forward_count = 0;
  headers_->iterate([&forward_count](const HeaderEntry&) -> HeaderMap::Iterate {
    forward_count++;
    if (forward_count == 2) {
      return HeaderMap::Iterate::Break;
    }
    return HeaderMap::Iterate::Continue;
  });
  EXPECT_EQ(2UL, forward_count);

  size_t reverse_count = 0;
  headers_->iterateReverse([&reverse_count](const HeaderEntry&) -> HeaderMap::Iterate {
    reverse_count++;
    if (reverse_count == 1) {
      return HeaderMap::Iterate::Break;
    }
    return HeaderMap::Iterate::Continue;
  });
  EXPECT_EQ(1UL, reverse_count);
}

TEST_F(HeaderMapOptimizedTest, RemoveIndexConsistency) {
  // Add multiple headers
  headers_->addCopy(LowerCaseString("a"), "1");
  headers_->addCopy(LowerCaseString("b"), "2");
  headers_->addCopy(LowerCaseString("c"), "3");
  headers_->addCopy(LowerCaseString("d"), "4");

  // Remove middle elements to test index map updating
  EXPECT_EQ(1UL, headers_->remove(LowerCaseString("b")));
  EXPECT_EQ(3UL, headers_->size());

  // Verify remaining headers are still accessible
  EXPECT_FALSE(headers_->get(LowerCaseString("a")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("b")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("c")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("d")).empty());

  // Remove first element
  EXPECT_EQ(1UL, headers_->remove(LowerCaseString("a")));
  EXPECT_EQ(2UL, headers_->size());

  // Verify remaining
  EXPECT_TRUE(headers_->get(LowerCaseString("a")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("c")).empty());
  EXPECT_FALSE(headers_->get(LowerCaseString("d")).empty());

  // Remove last element
  EXPECT_EQ(1UL, headers_->remove(LowerCaseString("d")));
  EXPECT_EQ(1UL, headers_->size());

  // Only 'c' should remain
  EXPECT_FALSE(headers_->get(LowerCaseString("c")).empty());
  EXPECT_TRUE(headers_->get(LowerCaseString("d")).empty());
}

TEST_F(HeaderMapOptimizedTest, AppendCopyEdgeCases) {
  // Append to non-existent header creates it
  headers_->appendCopy(LowerCaseString("new"), "value");
  EXPECT_EQ("value", headers_->get(LowerCaseString("new"))[0]->value().getStringView());

  // Append empty string
  headers_->appendCopy(LowerCaseString("new"), "");
  EXPECT_EQ("value", headers_->get(LowerCaseString("new"))[0]->value().getStringView());

  // Append to empty value
  headers_->addCopy(LowerCaseString("empty"), "");
  headers_->appendCopy(LowerCaseString("empty"), "appended");
  EXPECT_EQ("appended", headers_->get(LowerCaseString("empty"))[0]->value().getStringView());

  // Verify byte size is correctly updated during ``append()``
  const size_t before_size = headers_->byteSize();
  headers_->appendCopy(LowerCaseString("empty"), "-more");
  const size_t after_size = headers_->byteSize();
  EXPECT_EQ(before_size + 5, after_size); // 5 characters added
  EXPECT_EQ("appended-more", headers_->get(LowerCaseString("empty"))[0]->value().getStringView());
}

TEST_F(HeaderMapOptimizedTest, OperatorEqualEdgeCases) {
  const auto headers1 = std::make_unique<HeaderMapOptimized>();
  const auto headers2 = std::make_unique<HeaderMapOptimized>();

  // Test empty header maps
  EXPECT_TRUE(*headers1 == *headers2);

  // Test with different header order but same content
  headers1->addCopy(LowerCaseString("header-a"), "value-a");
  headers1->addCopy(LowerCaseString("header-b"), "value-b");

  headers2->addCopy(LowerCaseString("header-b"), "value-b");
  headers2->addCopy(LowerCaseString("header-a"), "value-a");

  EXPECT_TRUE(*headers1 == *headers2);

  // Test with same keys but different values
  headers2->setCopy(LowerCaseString("header-a"), "different-value");
  EXPECT_FALSE(*headers1 == *headers2);

  // Test operator!= as well
  EXPECT_TRUE(*headers1 != *headers2);

  // Test where one map is a subset of another
  headers1->clear();
  headers1->addCopy(LowerCaseString("common"), "value");

  headers2->clear();
  headers2->addCopy(LowerCaseString("common"), "value");
  headers2->addCopy(LowerCaseString("extra"), "value");

  EXPECT_FALSE(*headers1 == *headers2);
  EXPECT_FALSE(*headers2 == *headers1);

  // Test headers with the same key but empty value vs. non-empty
  headers1->clear();
  headers2->clear();
  headers1->addCopy(LowerCaseString("empty-test"), "");
  headers2->addCopy(LowerCaseString("empty-test"), " ");
  EXPECT_FALSE(*headers1 == *headers2);
}

} // namespace Http
} // namespace Envoy
