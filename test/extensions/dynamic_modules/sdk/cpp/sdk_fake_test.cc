#include "source/extensions/dynamic_modules/sdk/cpp/sdk_fake.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace DynamicModules {

// Compile-time inheritance checks.
static_assert(std::is_base_of_v<HeaderMap, FakeHeaderMap>);
static_assert(std::is_base_of_v<BodyBuffer, FakeBodyBuffer>);

// ---- FakeHeaderMap tests ----

TEST(FakeHeaderMapTest, GetMissingKeyReturnsEmpty) {
  FakeHeaderMap map;
  EXPECT_TRUE(map.get("x-missing").empty());
  EXPECT_TRUE(map.getOne("x-missing").empty());
}

TEST(FakeHeaderMapTest, SetAndGetSingleValue) {
  FakeHeaderMap map;
  map.set("content-type", "application/json");
  auto values = map.get("content-type");
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], "application/json");
  EXPECT_EQ(map.getOne("content-type"), "application/json");
}

TEST(FakeHeaderMapTest, SetReplacesExistingValues) {
  FakeHeaderMap map;
  map.add("x-custom", "first");
  map.add("x-custom", "second");
  map.set("x-custom", "replaced");
  auto values = map.get("x-custom");
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], "replaced");
}

TEST(FakeHeaderMapTest, AddAccumulatesValues) {
  FakeHeaderMap map;
  map.add("x-forwarded-for", "1.2.3.4");
  map.add("x-forwarded-for", "5.6.7.8");
  auto values = map.get("x-forwarded-for");
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], "1.2.3.4");
  EXPECT_EQ(values[1], "5.6.7.8");
  EXPECT_EQ(map.getOne("x-forwarded-for"), "1.2.3.4");
}

TEST(FakeHeaderMapTest, RemoveExistingKey) {
  FakeHeaderMap map;
  map.set("x-request-id", "abc");
  map.remove("x-request-id");
  EXPECT_TRUE(map.get("x-request-id").empty());
  EXPECT_EQ(map.size(), 0u);
}

TEST(FakeHeaderMapTest, RemoveMissingKeyIsNoOp) {
  FakeHeaderMap map;
  map.set("x-real-key", "val");
  map.remove("x-missing");
  EXPECT_EQ(map.size(), 1u);
}

TEST(FakeHeaderMapTest, SizeCountsAllValues) {
  FakeHeaderMap map;
  EXPECT_EQ(map.size(), 0u);
  map.add("x-a", "v1");
  map.add("x-a", "v2");
  map.set("x-b", "v3");
  // x-a has 2 values, x-b has 1 value → total 3
  EXPECT_EQ(map.size(), 3u);
}

TEST(FakeHeaderMapTest, GetAllReturnsAllHeaderViews) {
  FakeHeaderMap map;
  map.set("content-length", "42");
  map.add("x-custom", "a");
  map.add("x-custom", "b");
  auto all = map.getAll();
  ASSERT_EQ(all.size(), 3u);
  // Verify that data pointers are valid and content is correct.
  size_t content_length_count = 0;
  size_t x_custom_count = 0;
  for (const auto& hv : all) {
    if (hv.key() == "content-length") {
      EXPECT_EQ(hv.value(), "42");
      ++content_length_count;
    } else if (hv.key() == "x-custom") {
      ++x_custom_count;
    }
  }
  EXPECT_EQ(content_length_count, 1u);
  EXPECT_EQ(x_custom_count, 2u);
}

TEST(FakeHeaderMapTest, GetAllEmptyMap) {
  FakeHeaderMap map;
  EXPECT_TRUE(map.getAll().empty());
}

TEST(FakeHeaderMapTest, ClearRemovesAllHeaders) {
  FakeHeaderMap map;
  map.set("a", "1");
  map.set("b", "2");
  map.clear();
  EXPECT_EQ(map.size(), 0u);
  EXPECT_TRUE(map.getAll().empty());
}

// ---- FakeBodyBuffer tests ----

TEST(FakeBodyBufferTest, InitiallyEmpty) {
  FakeBodyBuffer buf;
  EXPECT_EQ(buf.getSize(), 0u);
  // getChunks() returns one empty view when empty.
  auto chunks = buf.getChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].size(), 0u);
}

TEST(FakeBodyBufferTest, AppendAndGetSize) {
  FakeBodyBuffer buf;
  buf.append("hello");
  EXPECT_EQ(buf.getSize(), 5u);
  buf.append(", world");
  EXPECT_EQ(buf.getSize(), 12u);
}

TEST(FakeBodyBufferTest, GetChunksReflectsContent) {
  FakeBodyBuffer buf;
  buf.append("hello, world");
  auto chunks = buf.getChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(std::string_view(chunks[0].data(), chunks[0].size()), "hello, world");
}

TEST(FakeBodyBufferTest, DrainRemovesFromFront) {
  FakeBodyBuffer buf;
  buf.append("hello, world");
  buf.drain(7);
  EXPECT_EQ(buf.getSize(), 5u);
  auto chunks = buf.getChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(std::string_view(chunks[0].data(), chunks[0].size()), "world");
}

TEST(FakeBodyBufferTest, DrainMoreThanSizeClampsToSize) {
  FakeBodyBuffer buf;
  buf.append("hi");
  buf.drain(100);
  EXPECT_EQ(buf.getSize(), 0u);
}

TEST(FakeBodyBufferTest, DrainZeroIsNoOp) {
  FakeBodyBuffer buf;
  buf.append("data");
  buf.drain(0);
  EXPECT_EQ(buf.getSize(), 4u);
}

TEST(FakeBodyBufferTest, ClearEmptiesBuffer) {
  FakeBodyBuffer buf;
  buf.append("some data");
  buf.clear();
  EXPECT_EQ(buf.getSize(), 0u);
}

} // namespace DynamicModules
} // namespace Envoy
