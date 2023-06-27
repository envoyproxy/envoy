#include "source/common/common/inline_map_registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(InlineMapWithZeroInlineKey, InlineMapWithZeroInlineKeyTest) {
  InlineMapRegistry<std::string> registry;
  registry.finalize();

  auto map = InlineMap<std::string, std::string>::create(registry);

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i), "value_" + std::to_string(i));
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i));
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
  InlineMapRegistry<std::string> registry;

  std::vector<InlineMapRegistry<std::string>::InlineKey> inline_keys;

  // Create 20 inline keys.
  for (size_t i = 0; i < 20; ++i) {
    inline_keys.push_back(registry.registerInlineKey("key_" + std::to_string(i)));
  }

  registry.finalize();

  auto map = InlineMap<std::string, std::string>::create(registry);

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i), "value_" + std::to_string(i));
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i));
  }

  // Lookup by typed inline handle.
  for (size_t i = 0; i < inline_keys.size(); ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = inline_keys[i];
    EXPECT_EQ(handle.inlineId(), i);
    EXPECT_EQ(*map->lookup(handle), "value_" + std::to_string(i));
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
