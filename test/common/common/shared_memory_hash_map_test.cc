#include <cstdint>
#include <memory>
#include <string>

#include "common/common/shared_memory_hash_map.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

namespace Envoy {

// Tests SharedMemoryHashMap.
class SharedMemoryHashTest : public testing::Test {
protected:
  typedef SharedMemoryHashMap<int64_t>::ValueCreatedPair ValueCreatedPair;

  void SetUp() override {
    options_.capacity = 100;
    options_.max_key_size = 250;
    options_.num_slots = 67;
    mem_size_ = SharedMemoryHashMap<int64_t>::numBytes(options_);
    memory_.reset(new uint8_t[mem_size_]);
  }

  SharedMemoryHashMapOptions options_;
  size_t mem_size_;
  std::unique_ptr<uint8_t[]> memory_;
};

TEST_F(SharedMemoryHashTest, initAndAttach) {
  {
    SharedMemoryHashMap<int64_t> hash_map1(options_);
    hash_map1.init(memory_.get());
    SharedMemoryHashMap<int64_t> hash_map2(options_);
    EXPECT_TRUE(hash_map2.attach(memory_.get(), mem_size_));
  }

  // If we pass in a memory-buffer with the wrong size, we can no longer attach it.
  {
    SharedMemoryHashMap<int64_t> hash_map3(options_);
    EXPECT_FALSE(hash_map3.attach(memory_.get(), mem_size_ - 1));
  }

  // If we tweak an option, we can no longer attach it.
  {
    options_.capacity = 99;
    SharedMemoryHashMap<int64_t> hash_map4(options_);
    EXPECT_FALSE(hash_map4.attach(memory_.get(), mem_size_));
  }
}

TEST_F(SharedMemoryHashTest, putRemove) {
  {
    SharedMemoryHashMap<int64_t> hash_map1(options_);

    hash_map1.init(memory_.get());
    EXPECT_TRUE(hash_map1.sanityCheck());
    EXPECT_EQ(0, hash_map1.size());
    EXPECT_EQ(nullptr, hash_map1.get("no such key"));
    ValueCreatedPair vc = hash_map1.put("good key");
    EXPECT_TRUE(vc.second);
    *vc.first = 12345;
    EXPECT_TRUE(hash_map1.sanityCheck());
    EXPECT_EQ(1, hash_map1.size());
    EXPECT_EQ(12345, *hash_map1.get("good key"));
    EXPECT_EQ(nullptr, hash_map1.get("no such key"));

    vc = hash_map1.put("good key");
    EXPECT_FALSE(vc.second) << "re-used, not newly created";
    *vc.first = 6789;
    EXPECT_EQ(6789, *hash_map1.get("good key"));
    EXPECT_EQ(1, hash_map1.size());
  }

  {
    // Now init a new hash-map with the same memory.
    SharedMemoryHashMap<int64_t> hash_map2(options_);
    EXPECT_TRUE(hash_map2.attach(memory_.get(), mem_size_));

    EXPECT_EQ(1, hash_map2.size());
    EXPECT_EQ(nullptr, hash_map2.get("no such key"));
    EXPECT_EQ(6789, *hash_map2.get("good key")) << hash_map2.toString();
    EXPECT_FALSE(hash_map2.remove("no such key"));
    EXPECT_TRUE(hash_map2.remove("good key"));
    EXPECT_EQ(nullptr, hash_map2.get("good key"));
    EXPECT_EQ(0, hash_map2.size());
  }
}

TEST_F(SharedMemoryHashTest, keyTooBig) {
  SharedMemoryHashMap<int64_t> hash_map1(options_);
  hash_map1.init(memory_.get());

  EXPECT_EQ(0, hash_map1.size());
  std::string big_key(300, 'a');
  EXPECT_EQ(nullptr, hash_map1.put(big_key).first) << big_key;
}

TEST_F(SharedMemoryHashTest, tooManyValues) {
  SharedMemoryHashMap<int64_t> hash_map1(options_);
  hash_map1.init(memory_.get());
  std::vector<std::string> keys;
  for (uint32_t i = 0; i < options_.capacity + 1; ++i) {
    keys.push_back(fmt::format("key{}", i));
  }

  for (uint32_t i = 0; i < options_.capacity; ++i) {
    int64_t* value = hash_map1.put(keys[i]).first;
    ASSERT_NE(nullptr, value);
    *value = i;
  }
  EXPECT_TRUE(hash_map1.sanityCheck());
  EXPECT_EQ(options_.capacity, hash_map1.size());

  for (uint32_t i = 0; i < options_.capacity; ++i) {
    const int64_t* value = hash_map1.get(keys[i]);
    ASSERT_NE(nullptr, value);
    EXPECT_EQ(i, *value);
  }
  EXPECT_TRUE(hash_map1.sanityCheck());

  // We can't fit one more value.
  EXPECT_EQ(nullptr, hash_map1.put(keys[options_.capacity]).first);
  EXPECT_TRUE(hash_map1.sanityCheck()) << hash_map1.toString();
  EXPECT_EQ(options_.capacity, hash_map1.size());

  // Now remove everything one by one.
  for (uint32_t i = 0; i < options_.capacity; ++i) {
    EXPECT_TRUE(hash_map1.remove(keys[i]));
  }
  EXPECT_TRUE(hash_map1.sanityCheck());
  EXPECT_EQ(0, hash_map1.size());

  // Now we can put in that last key we weren't able to before.
  int64_t* value = hash_map1.put(keys[options_.capacity]).first;
  EXPECT_NE(nullptr, value);
  *value = 314519;
  EXPECT_EQ(1, hash_map1.size());
  EXPECT_EQ(314519, *hash_map1.get(keys[options_.capacity]));
  EXPECT_TRUE(hash_map1.sanityCheck());
}

} // namespace Envoy
