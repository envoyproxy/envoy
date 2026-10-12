#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "source/common/common/persistent_hash_map.h"

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Hashes a key to itself so tests can place keys at exact positions in the tree. Keys that agree in
// their low 5 bits share a slot at the root. Keys that agree in their low 60 bits only differ at
// the deepest bitmap level.
struct IdentityHash {
  size_t operator()(uint64_t key) const { return key; }
};

// Hashes every key to the same value so all keys collide all the way down to the equal hash list.
struct ConstantHash {
  size_t operator()(uint64_t) const { return 0; }
};

using IdentityMap = PersistentHashMap<uint64_t, std::string, IdentityHash>;
using CollisionMap = PersistentHashMap<uint64_t, std::string, ConstantHash>;
using StringMap =
    PersistentHashMap<std::string, int, absl::Hash<absl::string_view>, std::equal_to<>>;

// Returns the entries visited by forEach keyed by their key so tests can compare against the
// expected contents.
template <class Key, class Value, class Hash, class Eq>
absl::flat_hash_map<Key, Value> collect(const PersistentHashMap<Key, Value, Hash, Eq>& map) {
  absl::flat_hash_map<Key, Value> entries;
  map.forEach([&entries](const Key& key, const Value& value) {
    EXPECT_TRUE(entries.emplace(key, value).second) << "visited twice";
  });
  EXPECT_EQ(map.size(), entries.size());
  return entries;
}

TEST(PersistentHashMapTest, Empty) {
  IdentityMap map;
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(0, map.size());
  EXPECT_EQ(nullptr, map.find(1));
  EXPECT_FALSE(map.erase(1));
  EXPECT_TRUE(collect(map).empty());

  const IdentityMap copy = map;
  EXPECT_TRUE(copy.empty());
  EXPECT_EQ(nullptr, copy.find(1));
}

TEST(PersistentHashMapTest, Move) {
  IdentityMap map;
  map.insert(1, "one");
  IdentityMap moved = std::move(map);
  EXPECT_EQ(1, moved.size());
  EXPECT_EQ("one", *moved.find(1));
  EXPECT_TRUE(map.empty()); // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(map.insert(2, "two"));
  EXPECT_EQ(1, map.size());

  map = std::move(moved);
  EXPECT_EQ(1, map.size());
  EXPECT_EQ("one", *map.find(1));
  EXPECT_EQ(nullptr, map.find(2));
  EXPECT_TRUE(moved.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(PersistentHashMapTest, InsertFindErase) {
  IdentityMap map;
  EXPECT_TRUE(map.insert(1, "one"));
  EXPECT_TRUE(map.insert(2, "two"));
  EXPECT_FALSE(map.empty());
  EXPECT_EQ(2, map.size());
  ASSERT_NE(nullptr, map.find(1));
  EXPECT_EQ("one", *map.find(1));
  EXPECT_EQ("two", *map.find(2));
  EXPECT_EQ(nullptr, map.find(3));

  // Inserting a present key keeps the existing entry.
  EXPECT_FALSE(map.insert(1, "uno"));
  EXPECT_EQ(2, map.size());
  EXPECT_EQ("one", *map.find(1));

  // Key 33 shares the root slot of key 1 but is absent.
  EXPECT_EQ(nullptr, map.find(33));
  EXPECT_FALSE(map.erase(33));
  EXPECT_EQ(2, map.size());

  EXPECT_TRUE(map.erase(1));
  EXPECT_FALSE(map.erase(1));
  EXPECT_EQ(1, map.size());
  EXPECT_EQ(nullptr, map.find(1));
  EXPECT_EQ("two", *map.find(2));

  EXPECT_TRUE(map.erase(2));
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(nullptr, map.find(2));
}

TEST(PersistentHashMapTest, CopyIsIsolatedSnapshot) {
  IdentityMap map;
  map.insert(1, "one");
  map.insert(2, "two");
  IdentityMap snapshot = map;

  map.insert(3, "three");
  map.erase(1);
  map.insert(1, "uno");
  map.erase(2);

  EXPECT_EQ(2, snapshot.size());
  EXPECT_EQ("one", *snapshot.find(1));
  EXPECT_EQ("two", *snapshot.find(2));
  EXPECT_EQ(nullptr, snapshot.find(3));

  EXPECT_EQ(2, map.size());
  EXPECT_EQ("uno", *map.find(1));
  EXPECT_EQ(nullptr, map.find(2));
  EXPECT_EQ("three", *map.find(3));

  // Updating the copy does not affect the original either.
  snapshot.insert(4, "four");
  snapshot.erase(1);
  EXPECT_EQ((absl::flat_hash_map<uint64_t, std::string>{{2, "two"}, {4, "four"}}),
            collect(snapshot));
  EXPECT_EQ((absl::flat_hash_map<uint64_t, std::string>{{1, "uno"}, {3, "three"}}), collect(map));

  // Dropping the original leaves the copy intact.
  map = IdentityMap();
  EXPECT_TRUE(map.empty());
  EXPECT_EQ((absl::flat_hash_map<uint64_t, std::string>{{2, "two"}, {4, "four"}}),
            collect(snapshot));
}

TEST(PersistentHashMapTest, ManyKeys) {
  constexpr uint64_t count = 10000;
  PersistentHashMap<uint64_t, uint64_t> map;
  for (uint64_t key = 0; key < count; ++key) {
    EXPECT_TRUE(map.insert(key, key * 2));
  }
  EXPECT_EQ(count, map.size());
  for (uint64_t key = 0; key < count; ++key) {
    ASSERT_NE(nullptr, map.find(key));
    EXPECT_EQ(key * 2, *map.find(key));
  }
  EXPECT_EQ(nullptr, map.find(count));
  EXPECT_EQ(count, collect(map).size());

  for (uint64_t key = 0; key < count; key += 2) {
    EXPECT_TRUE(map.erase(key));
  }
  EXPECT_EQ(count / 2, map.size());
  for (uint64_t key = 0; key < count; ++key) {
    EXPECT_EQ(key % 2 == 1, map.find(key) != nullptr);
  }
  for (uint64_t key = 1; key < count; key += 2) {
    EXPECT_TRUE(map.erase(key));
  }
  EXPECT_TRUE(map.empty());
  EXPECT_TRUE(collect(map).empty());
}

TEST(PersistentHashMapTest, HeterogeneousLookup) {
  StringMap map;
  map.insert("alpha", 1);
  map.insert(std::string("beta"), 2);
  const absl::string_view alpha = "alpha";
  ASSERT_NE(nullptr, map.find(alpha));
  EXPECT_EQ(1, *map.find(alpha));
  EXPECT_EQ(2, *map.find("beta"));
  EXPECT_EQ(nullptr, map.find(absl::string_view("gamma")));
  EXPECT_TRUE(map.erase(alpha));
  EXPECT_FALSE(map.erase(alpha));
  EXPECT_EQ(nullptr, map.find(alpha));
  EXPECT_EQ(1, map.size());
}

// Keys sharing a root slot are pushed into a child node and pulled back up as they are erased.
TEST(PersistentHashMapTest, SharedSlotSplitsAndCollapses) {
  IdentityMap map;
  // All share root slot 0. Keys 0 and 32 differ at depth 1. Key 1024 differs from key 0 only at
  // depth 2, so it forms a chain below the first child.
  map.insert(0, "a");
  map.insert(32, "b");
  map.insert(1024, "c");
  // Key 1 takes a different root slot and key 33 shares it, so that slot splits as well.
  map.insert(1, "d");
  map.insert(33, "e");
  EXPECT_EQ(5, map.size());
  EXPECT_EQ((absl::flat_hash_map<uint64_t, std::string>{
                {0, "a"}, {32, "b"}, {1024, "c"}, {1, "d"}, {33, "e"}}),
            collect(map));
  EXPECT_EQ(nullptr, map.find(64));
  EXPECT_EQ(nullptr, map.find(2048));
  EXPECT_FALSE(map.erase(64));
  EXPECT_FALSE(map.erase(2048));

  // A present key inside a child node is not replaced.
  EXPECT_FALSE(map.insert(1024, "C"));
  EXPECT_EQ("c", *map.find(1024));
  EXPECT_EQ(5, map.size());

  EXPECT_TRUE(map.erase(32));
  EXPECT_TRUE(map.erase(1024));
  EXPECT_EQ(3, map.size());
  EXPECT_EQ("a", *map.find(0));
  EXPECT_EQ(nullptr, map.find(32));
  EXPECT_EQ(nullptr, map.find(1024));

  // The slot is a plain entry again, so the next shared key splits it anew.
  EXPECT_TRUE(map.insert(64, "f"));
  EXPECT_EQ("a", *map.find(0));
  EXPECT_EQ("f", *map.find(64));
  EXPECT_EQ(4, map.size());

  // A split where the existing key has the larger fragment at the next depth.
  IdentityMap reversed;
  reversed.insert(32, "b");
  reversed.insert(0, "a");
  EXPECT_EQ("a", *reversed.find(0));
  EXPECT_EQ("b", *reversed.find(32));
  EXPECT_EQ((absl::flat_hash_map<uint64_t, std::string>{{0, "a"}, {32, "b"}}), collect(reversed));
}

// Keys that only differ at the deepest bitmap level form a chain of single child nodes.
TEST(PersistentHashMapTest, DeepChain) {
  IdentityMap map;
  const uint64_t high = uint64_t(1) << 60;
  map.insert(0, "low");
  map.insert(high, "high");
  EXPECT_EQ(2, map.size());
  EXPECT_EQ("low", *map.find(0));
  EXPECT_EQ("high", *map.find(high));
  EXPECT_EQ(nullptr, map.find(uint64_t(2) << 60));
  EXPECT_FALSE(map.erase(uint64_t(2) << 60));

  const IdentityMap snapshot = map;
  EXPECT_TRUE(map.erase(high));
  EXPECT_EQ(1, map.size());
  EXPECT_EQ("low", *map.find(0));
  EXPECT_EQ(nullptr, map.find(high));
  EXPECT_EQ(2, snapshot.size());
  EXPECT_EQ("high", *snapshot.find(high));

  EXPECT_TRUE(map.erase(0));
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(nullptr, map.find(0));
}

// Keys with equal hashes are kept in a list once the hash bits run out.
TEST(PersistentHashMapTest, EqualHashes) {
  CollisionMap map;
  EXPECT_TRUE(map.insert(1, "a"));
  EXPECT_TRUE(map.insert(2, "b"));
  EXPECT_TRUE(map.insert(3, "c"));
  EXPECT_EQ(3, map.size());
  EXPECT_EQ("a", *map.find(1));
  EXPECT_EQ("b", *map.find(2));
  EXPECT_EQ("c", *map.find(3));
  EXPECT_EQ(nullptr, map.find(4));
  EXPECT_FALSE(map.erase(4));
  EXPECT_EQ((absl::flat_hash_map<uint64_t, std::string>{{1, "a"}, {2, "b"}, {3, "c"}}),
            collect(map));

  EXPECT_FALSE(map.insert(2, "B"));
  EXPECT_EQ(3, map.size());
  EXPECT_EQ("b", *map.find(2));

  const CollisionMap snapshot = map;
  EXPECT_TRUE(map.erase(2));
  EXPECT_TRUE(map.erase(1));
  EXPECT_EQ(1, map.size());
  EXPECT_EQ("c", *map.find(3));
  EXPECT_EQ(nullptr, map.find(1));
  EXPECT_EQ(nullptr, map.find(2));
  EXPECT_EQ(3, snapshot.size());
  EXPECT_EQ("b", *snapshot.find(2));

  // The single survivor was pulled up to the root and can collide again.
  EXPECT_TRUE(map.insert(4, "d"));
  EXPECT_EQ("c", *map.find(3));
  EXPECT_EQ("d", *map.find(4));
  EXPECT_TRUE(map.erase(3));
  EXPECT_TRUE(map.erase(4));
  EXPECT_TRUE(map.empty());
}

// Snapshots stay readable from other threads while the owner keeps updating and releasing nodes.
TEST(PersistentHashMapTest, SnapshotsAreReadableConcurrently) {
  constexpr uint64_t count = 1000;
  PersistentHashMap<uint64_t, uint64_t> map;
  for (uint64_t key = 0; key < count; ++key) {
    map.insert(key, key);
  }
  const PersistentHashMap<uint64_t, uint64_t> snapshot = map;

  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&snapshot, count]() {
      for (int round = 0; round < 20; ++round) {
        const PersistentHashMap<uint64_t, uint64_t> copy = snapshot;
        for (uint64_t key = 0; key < count; ++key) {
          const uint64_t* value = copy.find(key);
          ASSERT_NE(nullptr, value);
          EXPECT_EQ(key, *value);
        }
        EXPECT_EQ(count, collect(copy).size());
      }
    });
  }
  for (uint64_t key = 0; key < count; ++key) {
    map.erase(key);
    map.insert(key + count, key);
  }
  for (std::thread& reader : readers) {
    reader.join();
  }
  EXPECT_EQ(count, map.size());
  EXPECT_EQ(nullptr, map.find(0));
  EXPECT_EQ(0, *map.find(count));
  EXPECT_EQ(count, snapshot.size());
  EXPECT_EQ(0, *snapshot.find(0));
}

// Once the snapshot sharing a node is released, the node is updated in place. The reader only
// signals the release through a relaxed flag, so the reference count alone has to order its reads
// before the in place update, which TSAN verifies.
TEST(PersistentHashMapTest, UpdateInPlaceAfterSnapshotReleasedOnOtherThread) {
  constexpr uint64_t count = 1000;
  PersistentHashMap<uint64_t, uint64_t> map;
  for (uint64_t key = 0; key < count; ++key) {
    map.insert(key, key);
  }

  std::atomic<bool> released{false};
  std::thread reader([snapshot = std::make_unique<const PersistentHashMap<uint64_t, uint64_t>>(map),
                      &released, count]() mutable {
    EXPECT_EQ(count, collect(*snapshot).size());
    snapshot.reset();
    released.store(true, std::memory_order_relaxed);
  });
  while (!released.load(std::memory_order_relaxed)) {
    std::this_thread::yield();
  }
  for (uint64_t key = 0; key < count; ++key) {
    EXPECT_TRUE(map.insert(key + count, key));
    EXPECT_TRUE(map.erase(key));
  }
  reader.join();
  EXPECT_EQ(count, map.size());
  EXPECT_EQ(nullptr, map.find(0));
  EXPECT_EQ(0, *map.find(count));
}

} // namespace
} // namespace Envoy
