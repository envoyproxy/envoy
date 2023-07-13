#include "source/common/common/inline_map.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(InlineMapWithZeroInlineKey, InlineMapWithZeroInlineKeyTest) {
  InlineMapDescriptor<std::string> descriptor;
  descriptor.finalize();

  auto map = InlineMap<std::string, std::string>::create(descriptor);

  // Insert keys.
  for (size_t i = 0; i < 100; ++i) {
    map->insert("key_" + std::to_string(i), "value_" + std::to_string(i));
  }

  // Insert duplicate keys will fail.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_FALSE(map->insert("key_" + std::to_string(i), "value_" + std::to_string(i)).second);
  }

  // Use operator[] to insert keys could overwrite existing keys.
  for (size_t i = 0; i < 100; ++i) {
    (*map)["key_" + std::to_string(i)] = "value_" + std::to_string(i) + "_new";
  }

  // Lookup keys.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i) + "_new");
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), OptRef<std::string>{});

  // Remove keys.
  for (size_t i = 0; i < 100; ++i) {
    map->remove("key_" + std::to_string(i));
  }
  map.reset();
}

TEST(InlineMapWith20InlineKey, InlineMapWith20InlineKeyTest) {
  InlineMapDescriptor<std::string> descriptor;

  std::vector<InlineMapDescriptor<std::string>::Handle> handles;

  // Create 20 inline keys.
  for (size_t i = 0; i < 20; ++i) {
    handles.push_back(descriptor.addInlineKey("key_" + std::to_string(i)));
  }

  // Add repeated inline keys will make no effect and return the same handle.
  for (size_t i = 0; i < 20; ++i) {
    EXPECT_EQ(handles[i].inlineId(),
              descriptor.addInlineKey("key_" + std::to_string(i)).inlineId());
  }

  descriptor.finalize();

  auto map = InlineMap<std::string, std::string>::create(descriptor);

  // Insert entries by normal keys. But these keys are registered as inline keys.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(map->insert("key_" + std::to_string(i), "value_" + std::to_string(i)).second);
  }

  // Insert duplicate keys will fail.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_FALSE(map->insert("key_" + std::to_string(i), "value_" + std::to_string(i)).second);
  }

  // Insert entries by typed inline handle.
  for (size_t i = 10; i < 20; ++i) {
    auto handle = handles[i];
    EXPECT_TRUE(map->insert(handle, "value_" + std::to_string(i)).second);
  }

  // Insert duplicate keys will fail.
  for (size_t i = 0; i < 20; ++i) {
    auto handle = handles[i];
    EXPECT_FALSE(map->insert(handle, "value_" + std::to_string(i)).second);
  }

  // Insert entries by normal keys.
  for (size_t i = 20; i < 100; ++i) {
    EXPECT_TRUE(map->insert("key_" + std::to_string(i), "value_" + std::to_string(i)).second);
  }

  // Insert duplicate keys will fail.
  for (size_t i = 20; i < 100; ++i) {
    EXPECT_FALSE(map->insert("key_" + std::to_string(i), "value_" + std::to_string(i)).second);
  }

  // Use operator[] to insert keys with typed inline handle could overwrite existing keys.
  for (size_t i = 0; i < 10; ++i) {
    (*map)[handles[i]] = "value_" + std::to_string(i) + "_new";
  }

  // Use operator[] to insert keys could overwrite existing keys (10-20 will be the keys that
  // registered as inline keys).
  for (size_t i = 10; i < 100; ++i) {
    (*map)["key_" + std::to_string(i)] = "value_" + std::to_string(i) + "_new";
  }

  // Lookup keys.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i) + "_new");
  }

  // Lookup by typed inline handle.
  for (size_t i = 0; i < handles.size(); ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = handles[i];
    EXPECT_EQ(handle.inlineId(), i);
    EXPECT_EQ(*map->lookup(handle), "value_" + std::to_string(i) + "_new");
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), OptRef<std::string>{});

  // Remove keys by typed inline handle.
  for (size_t i = 0; i < 10; ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = handles[i];
    map->remove(handle);
  }

  // Remove keys.
  for (size_t i = 10; i < 100; ++i) {
    map->remove("key_" + std::to_string(i));
  }

  EXPECT_EQ(map->size(), 0);

  // Lookup empty map by normal key will return nothing.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(map->lookup("key_" + std::to_string(i)), OptRef<std::string>{});
  }

  // Lookup empty map by typed inline handle will return nothing.
  for (auto handle : handles) {
    EXPECT_EQ(map->lookup(handle), OptRef<std::string>{});
  }

  // Operator[] will insert new entry if the key does not exist.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ((*map)[handles[i]], "");
  }

  for (size_t i = 10; i < 100; ++i) {
    EXPECT_EQ((*map)["key_" + std::to_string(i)], "");
  }

  EXPECT_EQ(map->size(), 100);

  map.reset();
}

} // namespace
} // namespace Envoy
