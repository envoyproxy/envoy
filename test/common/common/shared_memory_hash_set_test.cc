#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "common/common/fmt.h"
#include "common/common/hash.h"
#include "common/common/shared_memory_hash_set.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {

// Tests SharedMemoryHashSet.
class SharedMemoryHashSetTest : public testing::Test {
protected:
  // TestValue that doesn't define a hash.
  struct TestValueBase {
    absl::string_view key() const { return name; }
    void initialize(absl::string_view key) {
      size_t xfer = std::min(sizeof(name) - 1, key.size());
      memcpy(name, key.data(), xfer);
      name[xfer] = '\0';
    }
    static size_t size() { return sizeof(TestValue); }

    int64_t number;
    char name[256];
  };

  // TestValue that uses an always-zero hash.
  struct TestValueZeroHash : public TestValueBase {
    static uint64_t hash(absl::string_view /* key */) { return 0; }
  };

  // TestValue that uses a real hash function.
  struct TestValue : public TestValueBase {
    static uint64_t hash(absl::string_view key) { return HashUtil::xxHash64(key); }
  };

  typedef SharedMemoryHashSet<TestValue>::ValueCreatedPair ValueCreatedPair;

  template <class TestValueClass> void setUp() {
    options_.capacity = 100;
    options_.num_slots = 5;
    const uint32_t mem_size = SharedMemoryHashSet<TestValueClass>::numBytes(options_);
    memory_.reset(new uint8_t[mem_size]);
    memset(memory_.get(), 0, mem_size);
  }

  SharedMemoryHashSetOptions options_;
  std::unique_ptr<uint8_t[]> memory_;
};

TEST_F(SharedMemoryHashSetTest, initAndAttach) {
  setUp<TestValue>();
  {
    SharedMemoryHashSet<TestValue> hash_set1(options_, true, memory_.get());  // init
    SharedMemoryHashSet<TestValue> hash_set2(options_, false, memory_.get()); // attach
  }

  // If we tweak an option, we can no longer attach it.
  bool constructor_completed = false;
  bool constructor_threw = false;
  try {
    options_.capacity = 99;
    SharedMemoryHashSet<TestValue> hash_set3(options_, false, memory_.get());
    constructor_completed = false;
  } catch (const std::exception& e) {
    constructor_threw = true;
  }
  EXPECT_TRUE(constructor_threw);
  EXPECT_FALSE(constructor_completed);
}

TEST_F(SharedMemoryHashSetTest, putRemove) {
  setUp<TestValue>();
  {
    SharedMemoryHashSet<TestValue> hash_set1(options_, true, memory_.get());
    hash_set1.sanityCheck();
    EXPECT_EQ(0, hash_set1.size());
    EXPECT_EQ(nullptr, hash_set1.get("no such key"));
    ValueCreatedPair vc = hash_set1.insert("good key");
    EXPECT_TRUE(vc.second);
    vc.first->number = 12345;
    hash_set1.sanityCheck();
    EXPECT_EQ(1, hash_set1.size());
    EXPECT_EQ(12345, hash_set1.get("good key")->number);
    EXPECT_EQ(nullptr, hash_set1.get("no such key"));

    vc = hash_set1.insert("good key");
    EXPECT_FALSE(vc.second) << "re-used, not newly created";
    vc.first->number = 6789;
    EXPECT_EQ(6789, hash_set1.get("good key")->number);
    EXPECT_EQ(1, hash_set1.size());
  }

  {
    // Now init a new hash-map with the same memory.
    SharedMemoryHashSet<TestValue> hash_set2(options_, false, memory_.get());
    EXPECT_EQ(1, hash_set2.size());
    EXPECT_EQ(nullptr, hash_set2.get("no such key"));
    EXPECT_EQ(6789, hash_set2.get("good key")->number) << hash_set2.toString();
    EXPECT_FALSE(hash_set2.remove("no such key"));
    hash_set2.sanityCheck();
    EXPECT_TRUE(hash_set2.remove("good key"));
    hash_set2.sanityCheck();
    EXPECT_EQ(nullptr, hash_set2.get("good key"));
    EXPECT_EQ(0, hash_set2.size());
  }
}

TEST_F(SharedMemoryHashSetTest, tooManyValues) {
  setUp<TestValue>();
  SharedMemoryHashSet<TestValue> hash_set1(options_, true, memory_.get());
  std::vector<std::string> keys;
  for (uint32_t i = 0; i < options_.capacity + 1; ++i) {
    keys.push_back(fmt::format("key{}", i));
  }

  for (uint32_t i = 0; i < options_.capacity; ++i) {
    TestValue* value = hash_set1.insert(keys[i]).first;
    ASSERT_NE(nullptr, value);
    value->number = i;
  }
  hash_set1.sanityCheck();
  EXPECT_EQ(options_.capacity, hash_set1.size());

  for (uint32_t i = 0; i < options_.capacity; ++i) {
    const TestValue* value = hash_set1.get(keys[i]);
    ASSERT_NE(nullptr, value);
    EXPECT_EQ(i, value->number);
  }
  hash_set1.sanityCheck();

  // We can't fit one more value.
  EXPECT_EQ(nullptr, hash_set1.insert(keys[options_.capacity]).first);
  hash_set1.sanityCheck();
  EXPECT_EQ(options_.capacity, hash_set1.size());

  // Now remove everything one by one.
  for (uint32_t i = 0; i < options_.capacity; ++i) {
    EXPECT_TRUE(hash_set1.remove(keys[i]));
  }
  hash_set1.sanityCheck();
  EXPECT_EQ(0, hash_set1.size());

  // Now we can put in that last key we weren't able to before.
  TestValue* value = hash_set1.insert(keys[options_.capacity]).first;
  EXPECT_NE(nullptr, value);
  value->number = 314519;
  EXPECT_EQ(1, hash_set1.size());
  EXPECT_EQ(314519, hash_set1.get(keys[options_.capacity])->number);
  hash_set1.sanityCheck();
}

TEST_F(SharedMemoryHashSetTest, severalKeysZeroHash) {
  setUp<TestValueZeroHash>();
  SharedMemoryHashSet<TestValueZeroHash> hash_set1(options_, true, memory_.get());
  hash_set1.insert("one").first->number = 1;
  hash_set1.insert("two").first->number = 2;
  hash_set1.insert("three").first->number = 3;
  EXPECT_TRUE(hash_set1.remove("two"));
  hash_set1.sanityCheck();
  hash_set1.insert("four").first->number = 4;
  hash_set1.sanityCheck();
  EXPECT_FALSE(hash_set1.remove("two"));
  hash_set1.sanityCheck();
}

TEST_F(SharedMemoryHashSetTest, sanityCheckZeroedMemoryDeathTest) {
  setUp<TestValueZeroHash>();
  SharedMemoryHashSet<TestValueZeroHash> hash_set1(options_, true, memory_.get());
  memset(memory_.get(), 0, hash_set1.numBytes());
  EXPECT_DEATH(hash_set1.sanityCheck(), "");
}

} // namespace Envoy
