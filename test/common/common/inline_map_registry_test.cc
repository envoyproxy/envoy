#include "test/common/common/inline_map_registry_test_scope.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(InlineMapWithZeroInlineKey, InlineMapWithZeroInlineKeyTest) {
  auto map =
      InlineMapRegistry<InlineMapRegistryTestScope<0>>::InlineMap<std::string>::createInlineMap();

  EXPECT_EQ(InlineMapRegistryManager::registryInfos().size(), 1);
  EXPECT_EQ(InlineMapRegistryManager::registryInfos()[0].registry_scope_name,
            InlineMapRegistryTestScope<0>::name());

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i),
                std::make_unique<std::string>("value_" + std::to_string(i)));
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i));
  }

  // Lookup by untyped inline handle.
  for (size_t i = 0; i < 200; ++i) {
    const std::string key = "key_" + std::to_string(i);
    UntypedInlineHandle handle(InlineMapRegistry<InlineMapRegistryTestScope<0>>::scopeId(), i, key);
    EXPECT_EQ(*map->lookup(handle), "value_" + std::to_string(i));
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), nullptr);

  // remove keys by untyped inline handle.
  for (size_t i = 0; i < 100; ++i) {
    const std::string key = "key_" + std::to_string(i);
    UntypedInlineHandle handle(InlineMapRegistry<InlineMapRegistryTestScope<0>>::scopeId(), i, key);
    map->remove(handle);
  }

  // Remove keys.
  for (size_t i = 0; i < 200; ++i) {
    map->remove("key_" + std::to_string(i));
  }
}

TEST(InlineMapWith20InlineKey, InlineMapWith20InlineKeyTest) {

  auto inline_hanldes = InlineMapRegistryTestScope<20>::inlineHandles();

  auto map =
      InlineMapRegistry<InlineMapRegistryTestScope<20>>::InlineMap<std::string>::createInlineMap();

  // Insert keys.
  for (size_t i = 0; i < 200; ++i) {
    map->insert("key_" + std::to_string(i),
                std::make_unique<std::string>("value_" + std::to_string(i)));
  }

  // Lookup keys.
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(*map->lookup("key_" + std::to_string(i)), "value_" + std::to_string(i));
  }

  // Lookup by untyped inline handle.
  for (size_t i = 0; i < 200; ++i) {
    const std::string key = "key_" + std::to_string(i);
    UntypedInlineHandle handle(InlineMapRegistry<InlineMapRegistryTestScope<20>>::scopeId(), i,
                               key);
    EXPECT_EQ(*map->lookup(handle), "value_" + std::to_string(i));
  }

  // Lookup by typed inline handle.
  for (size_t i = 0; i < inline_hanldes.size(); ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = inline_hanldes[i];
    EXPECT_EQ(handle.inlineEntryKey(), key);
    EXPECT_EQ(handle.inlineEntryId(), i);
    EXPECT_EQ(*map->lookup(handle), "value_" + std::to_string(i));
  }

  // Lookup non-existing keys.
  EXPECT_EQ(map->lookup("non_existing_key"), nullptr);

  // Remove keys by typed inline handle.
  for (size_t i = 0; i < 10; ++i) {
    const std::string key = "key_" + std::to_string(i);
    auto handle = inline_hanldes[i];
    map->remove(handle);
  }

  // Remove keys by untyped inline handle.
  for (size_t i = 10; i < 30; ++i) {
    const std::string key = "key_" + std::to_string(i);
    UntypedInlineHandle handle(InlineMapRegistry<InlineMapRegistryTestScope<20>>::scopeId(), i,
                               key);
    map->remove(handle);
  }

  // Remove keys.
  for (size_t i = 30; i < 200; ++i) {
    map->remove("key_" + std::to_string(i));
  }

  EXPECT_EQ(map->size(), 0);
}

} // namespace
} // namespace Envoy
