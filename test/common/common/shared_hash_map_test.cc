#include <cstdint>
#include <memory>
#include <string>

#include "common/common/shared_hash_map.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

namespace Envoy {

class SharedHashTest : public testing::Test {
protected:
  void SetUp() override {
    options_.capacity = 100;
    options_.num_string_bytes = 1000;
    options_.num_slots = 67;
  }

  SharedHashMapOptions options_;
  std::unique_ptr<uint8_t[]> memory_;
};

TEST_F(SharedHashTest, initAndAttach) {
  {
    SharedHashMap<int64_t> hash_map1(options_);
    memory_.reset(new uint8_t[hash_map1.numBytes()]);
    hash_map1.init(memory_.get());
    SharedHashMap<int64_t> hash_map2(options_);
    EXPECT_TRUE(hash_map2.attach(memory_.get()));
  }

  // If we tweak an option, we can no longer attach it.
  {
    options_.capacity = 99;
    SharedHashMap<int64_t> hash_map3(options_);
    EXPECT_FALSE(hash_map3.attach(memory_.get()));
  }
}

TEST_F(SharedHashTest, setAndCheck) {
  {
    SharedHashMap<int64_t> hash_map1(options_);
    memory_.reset(new uint8_t[hash_map1.numBytes()]);

    hash_map1.init(memory_.get());
    EXPECT_TRUE(hash_map1.sanityCheck());
    EXPECT_EQ(0, hash_map1.size());
    EXPECT_EQ(nullptr, hash_map1.get("no such key"));
    *hash_map1.put("good key") = 12345;
    EXPECT_TRUE(hash_map1.sanityCheck());
    EXPECT_EQ(1, hash_map1.size());
    EXPECT_EQ(12345, *hash_map1.get("good key"));
    EXPECT_EQ(nullptr, hash_map1.get("no such key"));
  }

  {
    // Now init a new hash-map with the same memory.
    SharedHashMap<int64_t> hash_map2(options_);
    EXPECT_TRUE(hash_map2.attach(memory_.get()));

    EXPECT_EQ(1, hash_map2.size());
    EXPECT_EQ(nullptr, hash_map2.get("no such key"));
    EXPECT_EQ(12345, *hash_map2.get("good key"));
  }
}

TEST_F(SharedHashTest, keyTooBig) {
  SharedHashMap<int64_t> hash_map1(options_);
  memory_.reset(new uint8_t[hash_map1.numBytes()]);
  hash_map1.init(memory_.get());

  EXPECT_EQ(0, hash_map1.size());
  std::string big_key(300, 'a');
  EXPECT_EQ(nullptr, hash_map1.put(big_key)) << big_key;
}

TEST_F(SharedHashTest, tooManyValues) {
  SharedHashMap<int64_t> hash_map1(options_);
  memory_.reset(new uint8_t[hash_map1.numBytes()]);
  hash_map1.init(memory_.get());

  for (uint32_t i = 0; i < options_.capacity; ++i) {
    int64_t* value = hash_map1.put(fmt::format("key{}", i));
    ASSERT_NE(nullptr, value);
    *value = i;
  }
  EXPECT_TRUE(hash_map1.sanityCheck());

  for (uint32_t i = 0; i < options_.capacity; ++i) {
    const int64_t* value = hash_map1.get(fmt::format("key{}", i));
    ASSERT_NE(nullptr, value);
    EXPECT_EQ(i, *value);
  }
  EXPECT_TRUE(hash_map1.sanityCheck());

  // We can't fit one more value.
  EXPECT_EQ(nullptr, hash_map1.put(fmt::format("key{}", options_.capacity)));
  EXPECT_TRUE(hash_map1.sanityCheck());
}

TEST_F(SharedHashTest, tooManyKeyBytes) {
  SharedHashMap<int64_t> hash_map1(options_);
  memory_.reset(new uint8_t[hash_map1.numBytes()]);
  hash_map1.init(memory_.get());

  std::string big_key_prefix(200, 'a');

  // Key size doesn't have to be exact here. 4 keys will easily fit even with slop.
  const int num_200_byte_keys_that_will_fit = options_.num_string_bytes / 201;
  for (int i = 0; i < num_200_byte_keys_that_will_fit; ++i) {
    int64_t* value = hash_map1.put(fmt::format("{}{}", big_key_prefix, i));
    ASSERT_NE(nullptr, value);
    *value = i;
  }
  EXPECT_TRUE(hash_map1.sanityCheck());

  // The next key will not fit in our char-space.
  std::string key = fmt::format("{}{}", big_key_prefix, num_200_byte_keys_that_will_fit);
  EXPECT_EQ(nullptr, hash_map1.put(key)) << key;
  EXPECT_TRUE(hash_map1.sanityCheck());
}

} // namespace Envoy
