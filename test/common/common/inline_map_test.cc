#include "source/common/common/inline_map.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(InlineMapWithZeroInlineKey, InlineMapWithZeroInlineKeyTest) {
  InlineMapDescriptor<std::string> descriptor;
  descriptor.finalize();

  auto map = InlineMap<std::string, std::string>::create(descriptor);

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i), "value_" + std::to_string(i));
  }

  // Insert duplicate keys will fail.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_FALSE(map->insert("key_" + std::to_string(i), "value_" + std::to_string(i)));
  }

  // Use operator[] to insert keys could overwrite existing keys.
  for (size_t i = 0; i < 200; ++i) {
    (*map)["key_" + std::to_string(i)] = "value_" + std::to_string(i) + "_new";
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i) + "_new");
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), OptRef<std::string>{});

  // Remove keys.
  for (size_t i = 0; i < 200; ++i) {
    map->remove("key_" + std::to_string(i));
  }
  map.release();
}

TEST(InlineMapWith20InlineKey, InlineMapWith20InlineKeyTest) {
  InlineMapDescriptor<std::string> descriptor;

  std::vector<InlineMapDescriptor<std::string>::InlineKey> inline_keys;

  // Create 20 inline keys.
  for (size_t i = 0; i < 20; ++i) {
    inline_keys.push_back(descriptor.addInlineKey("key_" + std::to_string(i)));
  }

  descriptor.finalize();

  auto map = InlineMap<std::string, std::string>::create(descriptor);

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i), "value_" + std::to_string(i));
  }

  // Insert duplicate keys will fail.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_FALSE(map->insert("key_" + std::to_string(i), "value_" + std::to_string(i)));
  }

  // Insert duplicate keys with typed inline handle will fail.
  for (size_t i = 0; i < 20; ++i) {
    EXPECT_FALSE(map->insert(inline_keys[i], "value_" + std::to_string(i)));
  }

  // Use operator[] to insert keys could overwrite existing keys.
  for (size_t i = 20; i < 200; ++i) {
    (*map)["key_" + std::to_string(i)] = "value_" + std::to_string(i) + "_new";
  }

  // Use operator[] to insert keys with typed inline handle could overwrite existing keys.
  for (size_t i = 0; i < 20; ++i) {
    (*map)[inline_keys[i]] = "value_" + std::to_string(i) + "_new";
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i) + "_new");
  }

  // Lookup by typed inline handle.
  for (size_t i = 0; i < inline_keys.size(); ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = inline_keys[i];
    EXPECT_EQ(handle.inlineId(), i);
    EXPECT_EQ(*map->lookup(handle), "value_" + std::to_string(i) + "_new");
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), OptRef<std::string>{});

  // Remove keys by typed inline handle.
  for (size_t i = 0; i < 10; ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = inline_keys[i];
    map->remove(handle);
  }

  // Remove keys.
  for (size_t i = 10; i < 200; ++i) {
    map->remove("key_" + std::to_string(i));
  }

  EXPECT_EQ(map->size(), 0);

  map.release();
}

} // namespace
} // namespace Envoy
