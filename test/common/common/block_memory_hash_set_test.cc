#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "common/common/block_memory_hash_set.h"
#include "common/common/fmt.h"
#include "common/common/hash.h"
#include "common/stats/stats_options_impl.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {

// Tests BlockMemoryHashSet.
class BlockMemoryHashSetTest : public testing::Test {
protected:
  // TestValue that doesn't define a hash.
  struct TestValueBase {
    absl::string_view key() const { return name; }
    void initialize(absl::string_view key, const Stats::StatsOptions& stats_options) {
      ASSERT(key.size() <= stats_options.maxNameLength());
      memcpy(name, key.data(), key.size());
      name[key.size()] = '\0';
    }
    static uint64_t structSizeWithOptions(const Stats::StatsOptions& stats_options) {
      UNREFERENCED_PARAMETER(stats_options);
      return sizeof(TestValue);
    }

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

  typedef BlockMemoryHashSet<TestValue>::ValueCreatedPair ValueCreatedPair;

  template <class TestValueClass> void setUp() {
    hash_set_options_.capacity = 100;
    hash_set_options_.num_slots = 5;
    const uint32_t mem_size =
        BlockMemoryHashSet<TestValueClass>::numBytes(hash_set_options_, stats_options_);
    memory_.reset(new uint8_t[mem_size]);
    memset(memory_.get(), 0, mem_size);
  }

  /**
   * Returns a string describing the contents of the map, including the control
   * bits and the keys in each slot.
   */
  template <class TestValueClass>
  std::string hashSetToString(BlockMemoryHashSet<TestValueClass>& hs) {
    std::string ret;
    static const uint32_t sentinal = BlockMemoryHashSet<TestValueClass>::Sentinal;
    std::string control_string =
        fmt::format("{} size={} free_cell_index={}", hs.control_->hash_set_options.toString(),
                    hs.control_->size, hs.control_->free_cell_index);
    ret = fmt::format("options={}\ncontrol={}\n", hs.control_->hash_set_options.toString(),
                      control_string);
    for (uint32_t i = 0; i < hs.control_->hash_set_options.num_slots; ++i) {
      ret += fmt::format("slot {}:", i);
      for (uint32_t j = hs.slots_[i]; j != sentinal; j = hs.getCell(j).next_cell_index) {
        ret += " " + std::string(hs.getCell(j).value.key());
      }
      ret += "\n";
    }
    return ret;
  }

  BlockMemoryHashSetOptions hash_set_options_;
  Stats::StatsOptionsImpl stats_options_;
  std::unique_ptr<uint8_t[]> memory_;
};

TEST_F(BlockMemoryHashSetTest, initAndAttach) {
  setUp<TestValue>();
  {
    BlockMemoryHashSet<TestValue> hash_set1(hash_set_options_, true, memory_.get(),
                                            stats_options_); // init
    BlockMemoryHashSet<TestValue> hash_set2(hash_set_options_, false, memory_.get(),
                                            stats_options_); // attach
  }

  // If we tweak an option, we can no longer attach it.
  bool constructor_completed = false;
  bool constructor_threw = false;
  try {
    hash_set_options_.capacity = 99;
    BlockMemoryHashSet<TestValue> hash_set3(hash_set_options_, false, memory_.get(),
                                            stats_options_);
    constructor_completed = false;
  } catch (const std::exception& e) {
    constructor_threw = true;
  }
  EXPECT_TRUE(constructor_threw);
  EXPECT_FALSE(constructor_completed);
}

TEST_F(BlockMemoryHashSetTest, putRemove) {
  setUp<TestValue>();
  {
    BlockMemoryHashSet<TestValue> hash_set1(hash_set_options_, true, memory_.get(), stats_options_);
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
    BlockMemoryHashSet<TestValue> hash_set2(hash_set_options_, false, memory_.get(),
                                            stats_options_);
    EXPECT_EQ(1, hash_set2.size());
    EXPECT_EQ(nullptr, hash_set2.get("no such key"));
    EXPECT_EQ(6789, hash_set2.get("good key")->number) << hashSetToString<TestValue>(hash_set2);
    EXPECT_FALSE(hash_set2.remove("no such key"));
    hash_set2.sanityCheck();
    EXPECT_TRUE(hash_set2.remove("good key"));
    hash_set2.sanityCheck();
    EXPECT_EQ(nullptr, hash_set2.get("good key"));
    EXPECT_EQ(0, hash_set2.size());
  }
}

TEST_F(BlockMemoryHashSetTest, tooManyValues) {
  setUp<TestValue>();
  BlockMemoryHashSet<TestValue> hash_set1(hash_set_options_, true, memory_.get(), stats_options_);
  std::vector<std::string> keys;
  for (uint32_t i = 0; i < hash_set_options_.capacity + 1; ++i) {
    keys.push_back(fmt::format("key{}", i));
  }

  for (uint32_t i = 0; i < hash_set_options_.capacity; ++i) {
    TestValue* value = hash_set1.insert(keys[i]).first;
    ASSERT_NE(nullptr, value);
    value->number = i;
  }
  hash_set1.sanityCheck();
  EXPECT_EQ(hash_set_options_.capacity, hash_set1.size());

  for (uint32_t i = 0; i < hash_set_options_.capacity; ++i) {
    const TestValue* value = hash_set1.get(keys[i]);
    ASSERT_NE(nullptr, value);
    EXPECT_EQ(i, value->number);
  }
  hash_set1.sanityCheck();

  // We can't fit one more value.
  EXPECT_EQ(nullptr, hash_set1.insert(keys[hash_set_options_.capacity]).first);
  hash_set1.sanityCheck();
  EXPECT_EQ(hash_set_options_.capacity, hash_set1.size());

  // Now remove everything one by one.
  for (uint32_t i = 0; i < hash_set_options_.capacity; ++i) {
    EXPECT_TRUE(hash_set1.remove(keys[i]));
  }
  hash_set1.sanityCheck();
  EXPECT_EQ(0, hash_set1.size());

  // Now we can put in that last key we weren't able to before.
  TestValue* value = hash_set1.insert(keys[hash_set_options_.capacity]).first;
  EXPECT_NE(nullptr, value);
  value->number = 314519;
  EXPECT_EQ(1, hash_set1.size());
  EXPECT_EQ(314519, hash_set1.get(keys[hash_set_options_.capacity])->number);
  hash_set1.sanityCheck();
}

TEST_F(BlockMemoryHashSetTest, severalKeysZeroHash) {
  setUp<TestValueZeroHash>();
  BlockMemoryHashSet<TestValueZeroHash> hash_set1(hash_set_options_, true, memory_.get(),
                                                  stats_options_);
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

TEST_F(BlockMemoryHashSetTest, sanityCheckZeroedMemoryDeathTest) {
  setUp<TestValueZeroHash>();
  BlockMemoryHashSet<TestValueZeroHash> hash_set1(hash_set_options_, true, memory_.get(),
                                                  stats_options_);
  memset(memory_.get(), 0, hash_set1.numBytes(stats_options_));
  EXPECT_DEATH(hash_set1.sanityCheck(), "");
}

} // namespace Envoy
