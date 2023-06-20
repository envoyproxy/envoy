#include "source/common/common/inline_map_registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(InlineMapWithZeroInlineKey, InlineMapWithZeroInlineKeyTest) {
  InlineMapRegistry registry;
  registry.finalize();

  auto map = registry.createInlineMap<std::string>();

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i),
                std::make_unique<std::string>("value_" + std::to_string(i)));
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i));
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), nullptr);

  // Remove keys.
  for (size_t i = 0; i < 200; ++i) {
    map->remove("key_" + std::to_string(i));
  }
}

TEST(InlineMapWith20InlineKey, InlineMapWith20InlineKeyTest) {
  InlineMapRegistry registry;

  std::vector<InlineMapRegistry::InlineKey> inline_keys;

  // Create 20 inline keys.
  for (size_t i = 0; i < 20; ++i) {
    inline_keys.push_back(registry.registerInlineKey("key_" + std::to_string(i)));
  }

  registry.finalize();

  auto map = registry.createInlineMap<std::string>();

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i),
                std::make_unique<std::string>("value_" + std::to_string(i)));
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i));
  }

  // Lookup by typed inline handle.
  for (size_t i = 0; i < inline_keys.size(); ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = inline_keys[i];
    EXPECT_EQ(handle.inlineKey(), key);
    EXPECT_EQ(handle.inlineId(), i);
    EXPECT_EQ(*map->lookup(handle), "value_" + std::to_string(i));
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), nullptr);

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
}

} // namespace
} // namespace Envoy
