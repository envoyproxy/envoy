// Copyright 2016 Google Inc.
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

//
// Tests fo SimpleLRUCache

#include <math.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <list>
#include <memory>

#include "source/common/jwt/simple_lru_cache.h"
#include "source/common/jwt/simple_lru_cache_inl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::HasSubstr;
using ::testing::NotNull;

namespace Envoy {
namespace SimpleLruCache {

// Keep track of whether or not specific values are in the cache
static const int kElems = 100;
static const int kCacheSize = 10;
static bool in_cache[kElems];

namespace {

// Blocks until SimpleCycleTimer::Now() returns a new value.
void tickClock() {
  int64_t start = SimpleCycleTimer::now();
  const int kMaxAttempts = 10;
  int num_attempts = 0;
  do {
    // sleep one microsecond.
    usleep(1);
  } while (++num_attempts < kMaxAttempts && SimpleCycleTimer::now() == start);
  // Unable to tick the clock
  assert(num_attempts < kMaxAttempts);
}

} // namespace

// Value type
struct TestValue {
  int label; // Index into "in_cache"
  explicit TestValue(int l) : label(l) {}

protected:
  // Make sure that TestCache can delete TestValue when declared as friend.
  friend class SimpleLRUCache<int, TestValue>;
  friend class TestCache;
  ~TestValue() {}
};

class TestCache : public SimpleLRUCache<int, TestValue> {
public:
  explicit TestCache(int64_t size, bool check_in_cache = true)
      : SimpleLRUCache<int, TestValue>(size), check_in_cache_(check_in_cache) {}

protected:
  virtual void removeElement(TestValue* v) {
    if (v && check_in_cache_) {
      assert(in_cache[v->label]);
      std::cout << " Evict:" << v->label;
      in_cache[v->label] = false;
    }
    delete v;
  }

  const bool check_in_cache_;
};

class SimpleLRUCacheTest : public ::testing::Test {
protected:
  SimpleLRUCacheTest() {}
  virtual ~SimpleLRUCacheTest() {}

  virtual void SetUp() {
    for (int i = 0; i < kElems; ++i)
      in_cache[i] = false;
  }

  virtual void TearDown() {
    if (cache_)
      cache_->clear();
    for (int i = 0; i < kElems; i++) {
      assert(!in_cache[i]);
    }
  }

  void testInOrderEvictions(int cache_size);
  void testSetMaxSize();
  void testOverfullEvictionPolicy();
  void testRemoveUnpinned();
  void testExpiration(bool lru, bool release_quickly);
  void testLargeExpiration(bool lru, double timeout);

  std::unique_ptr<TestCache> cache_;
};

TEST_F(SimpleLRUCacheTest, IteratorDefaultConstruct) { TestCache::const_iterator default_unused; }

TEST_F(SimpleLRUCacheTest, Iteration) {
  int count = 0;
  cache_.reset(new TestCache(kCacheSize));

  // fill the cache, evict some items, ensure i can iterate over all remaining
  for (int i = 0; i < kElems; ++i) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  for (TestCache::const_iterator pos = cache_->begin(); pos != cache_->end(); ++pos) {
    ++count;
    ASSERT_EQ(pos->first, pos->second->label);
    ASSERT_TRUE(in_cache[pos->second->label]);
  }
  ASSERT_EQ(count, kCacheSize);
  ASSERT_EQ(cache_->entries(), kCacheSize);
  cache_->clear();

  // iterate over the cache w/o filling the cache to capacity first
  for (int i = 0; i < kCacheSize / 2; ++i) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  count = 0;
  for (TestCache::const_iterator pos = cache_->begin(); pos != cache_->end(); ++pos) {
    ++count;
    ASSERT_EQ(pos->first, pos->second->label);
    ASSERT_TRUE(in_cache[pos->second->label]);
  }
  ASSERT_EQ(count, kCacheSize / 2);
  ASSERT_EQ(cache_->entries(), kCacheSize / 2);
}

TEST_F(SimpleLRUCacheTest, StdCopy) {
  cache_.reset(new TestCache(kCacheSize));
  for (int i = 0; i < kElems; ++i) {
    in_cache[i] = true;
    cache_->insertPinned(i, new TestValue(i), 1);
  }
  // All entries are pinned, they are all in cache
  ASSERT_EQ(cache_->entries(), kElems);
  ASSERT_EQ(cache_->pinnedSize(), kElems);
  // Non have been removed, so Defer size is 0
  ASSERT_EQ(cache_->deferredEntries(), 0);

  std::vector<std::pair<int, TestValue*>> to_release;
  std::copy(cache_->begin(), cache_->end(), std::back_inserter(to_release));
  for (const auto& entry : to_release) {
    cache_->release(entry.first, entry.second);
  }

  // After all of them un-pinned
  ASSERT_EQ(cache_->entries(), kCacheSize);
  ASSERT_EQ(cache_->pinnedSize(), 0);
  ASSERT_EQ(cache_->deferredEntries(), 0);
}

void SimpleLRUCacheTest::testInOrderEvictions(int cache_size) {
  for (int i = 0; i < kElems; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);

    if (i >= cache_size) {
      ASSERT_TRUE(!in_cache[i - cache_size]);
    }
  }
}

TEST_F(SimpleLRUCacheTest, InOrderEvictions) {
  cache_.reset(new TestCache(kCacheSize));
  testInOrderEvictions(kCacheSize);
}

TEST_F(SimpleLRUCacheTest, InOrderEvictionsWithIdleEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize));
  cache_->setMaxIdleSeconds(2000);
  testInOrderEvictions(kCacheSize);
}

TEST_F(SimpleLRUCacheTest, InOrderEvictionsWithAgeBasedEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize));
  cache_->setAgeBasedEviction(2000);
  testInOrderEvictions(kCacheSize);
}

void SimpleLRUCacheTest::testSetMaxSize() {
  int cache_size = cache_->maxSize();

  // Fill the cache exactly and verify all values are present.
  for (int i = 0; i < cache_size; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  EXPECT_EQ(cache_size, cache_->size());
  int elems = cache_size;
  for (int i = 0; i < elems; i++) {
    ASSERT_TRUE(in_cache[i]) << i;
  }

  // Double the size; all values should still be present.
  cache_size *= 2;
  ASSERT_LE(cache_size, kElems);
  cache_->setMaxSize(cache_size);
  EXPECT_EQ(elems, cache_->size());
  for (int i = 0; i < elems; i++) {
    ASSERT_TRUE(in_cache[i]) << i;
  }

  // Fill the cache to the new size and ensure all values are present.
  for (int i = elems; i < cache_size; i++) {
    ASSERT_TRUE(!cache_->lookup(i)) << i;
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  EXPECT_EQ(cache_size, cache_->size());
  elems = cache_size;
  for (int i = 0; i < cache_size; i++) {
    ASSERT_TRUE(in_cache[i]) << i;
  }

  // Cut the size to half of the original size, elements should be evicted.
  cache_size /= 4;
  ASSERT_GT(cache_size, 0);
  cache_->setMaxSize(cache_size);
  EXPECT_EQ(cache_size, cache_->size());
  for (int i = 0; i < elems; i++) {
    if (i < elems - cache_size) {
      ASSERT_TRUE(!in_cache[i]) << i;
    } else {
      ASSERT_TRUE(in_cache[i]) << i;
    }
  }

  // Clear the cache and run the in order evictions test with the final size.
  cache_->clear();
  testInOrderEvictions(cache_size);
  EXPECT_EQ(cache_size, cache_->size());
}

TEST_F(SimpleLRUCacheTest, SetMaxSize) {
  cache_.reset(new TestCache(kCacheSize));
  testSetMaxSize();
}

TEST_F(SimpleLRUCacheTest, SetMaxSizeWithIdleEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize));
  cache_->setMaxIdleSeconds(2000);
  testSetMaxSize();
}

TEST_F(SimpleLRUCacheTest, SetMaxSizeWithAgeBasedEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize));
  cache_->setAgeBasedEviction(2000);
  testSetMaxSize();
}

TEST_F(SimpleLRUCacheTest, VoidValues) {
  //
  // This naive code may double-pin at Lookup() the second time
  // around if GetThing() returns 0 (which may be ok):
  //
  // Thing* thing = cache.Lookup(key);
  // if (!thing) {
  //   thing = GetThing(key);
  //   cache.InsertPinned(key, thing, 1);
  // }
  // UseThing(thing);
  // cache.Release(key, thing);
  //
  // One cannot distinguish between "not present" and "nullptr value" using
  // return value from Lookup(), so let's do it with StillInUse().
  //

  cache_.reset(new TestCache(1));

  cache_->insertPinned(5, 0, 1);
  cache_->release(5, 0);

  if (cache_->stillInUse(5, 0)) {
    // Released, but still in there
    // This path is executed given Dec 2007 implementation

    // Lookup pins 5, even though it returns nullptr
    ASSERT_TRUE(nullptr == cache_->lookup(5));
  } else {
    // Not in there, let's insert it
    // This path is not executed given Dec 2007 implementation
    cache_->insertPinned(5, 0, 1);
  }

  ASSERT_EQ(1, cache_->pinnedSize());
  cache_->release(5, 0);
  ASSERT_EQ(0, cache_->pinnedSize());

  cache_->clear();
}

void SimpleLRUCacheTest::testOverfullEvictionPolicy() {
  // Fill with elements that should stick around if used over and over
  for (int i = 0; i < kCacheSize; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }

  for (int i = kCacheSize; i < kElems; i++) {
    // Access all of the elements that should stick around
    for (int j = 0; j < kCacheSize; j++) {
      TestValue* v = cache_->lookup(j);
      ASSERT_TRUE(v != nullptr);
      cache_->release(j, v);
    }

    // Insert new value
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
    ASSERT_TRUE(in_cache[i]);
    if (i > kCacheSize) {
      ASSERT_TRUE(!in_cache[i - 1]);
    }
  }
}

TEST_F(SimpleLRUCacheTest, OverfullEvictionPolicy) {
  cache_.reset(new TestCache(kCacheSize + 1));
  testOverfullEvictionPolicy();
}

TEST_F(SimpleLRUCacheTest, OverfullEvictionPolicyWithIdleEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize + 1));
  // Here we are not testing idle eviction, just that LRU eviction
  // still works correctly when the cache is overfull.
  cache_->setMaxIdleSeconds(2000);
  testOverfullEvictionPolicy();
}

TEST_F(SimpleLRUCacheTest, OverfullEvictionPolicyWithAgeBasedEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize));
  // With age-based eviction usage is ignored and instead the oldest inserted
  // element is evicted when cache becomes overfull.
  cache_->setAgeBasedEviction(2000);

  for (int i = 0; i < kCacheSize; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }

  // Access all of the elements in the reverse order.
  for (int j = kCacheSize - 1; j >= 0; j--) {
    TestCache::ScopedLookup lv(cache_.get(), j);
    ASSERT_TRUE(lv.value() != nullptr);
  }

  // Key 0 was accessed most recently, yet new value evicts it because it is
  // the oldest one.
  ASSERT_TRUE(!cache_->lookup(kCacheSize));
  TestValue* v = new TestValue(kCacheSize);
  in_cache[kCacheSize] = true;
  cache_->insert(kCacheSize, v, 1);
  ASSERT_TRUE(in_cache[kCacheSize]);
  ASSERT_TRUE(!in_cache[0]);
}

TEST_F(SimpleLRUCacheTest, Update) {
  cache_.reset(new TestCache(kCacheSize, false)); // Don't check in_cache.
  // Insert some values.
  for (int i = 0; i < kCacheSize; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    cache_->insert(i, v, 1);
  }
  // Update them.
  for (int i = 0; i < kCacheSize; i++) {
    TestCache::ScopedLookup lookup(cache_.get(), i);
    ASSERT_TRUE(lookup.found());
    EXPECT_TRUE(lookup.value()->label == i);
    lookup.value()->label = -i;
  }
  // Read them back.
  for (int i = 0; i < kCacheSize; i++) {
    TestCache::ScopedLookup lookup(cache_.get(), i);
    ASSERT_TRUE(lookup.found());
    EXPECT_TRUE(lookup.value()->label == -i);
  }
  // Flush them out.
  for (int i = 0; i < kCacheSize; i++) {
    TestValue* v = new TestValue(i);
    cache_->insert(i + kCacheSize, v, 1);
  }
  // Original values are gone.
  for (int i = 0; i < kCacheSize; i++) {
    TestCache::ScopedLookup lookup(cache_.get(), i + kCacheSize);
    ASSERT_TRUE(lookup.found());
    TestCache::ScopedLookup lookup2(cache_.get(), i);
    ASSERT_TRUE(!lookup2.found());
  }
}

TEST_F(SimpleLRUCacheTest, Pinning) {
  static const int kPinned = kCacheSize + 4;
  cache_.reset(new TestCache(kCacheSize));
  for (int i = 0; i < kElems; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    if (i < kPinned) {
      cache_->insertPinned(i, v, 1);
    } else {
      cache_->insert(i, v, 1);
    }
  }
  for (int i = 0; i < kPinned; i++) {
    ASSERT_TRUE(in_cache[i]);
    TestValue* v = cache_->lookup(i);
    ASSERT_TRUE(v != nullptr);
    cache_->release(i, v); // For initial insertPinned
    cache_->release(i, v); // For the previous lookup
  }
}

TEST_F(SimpleLRUCacheTest, Remove) {
  cache_.reset(new TestCache(kCacheSize));
  for (int i = 0; i < kElems; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);

    // Remove previous element, but leave "0" alone
    if (i > 1) {
      const int key = i - 1;
      int prev_entries = cache_->entries();
      if ((key % 2) == 0) { // test normal removal
        cache_->remove(key);
      } else { // test different removal status
        TestValue* const v2 = cache_->lookup(key);
        ASSERT_TRUE(v2) << ": key=" << key;
        cache_->remove(key);
        ASSERT_TRUE(cache_->stillInUse(key)) << ": " << key;
        cache_->remove(key);
        ASSERT_TRUE(cache_->stillInUse(key)) << ": " << key;

        cache_->release(key, v2);
      }
      ASSERT_EQ(cache_->entries(), prev_entries - 1);
      ASSERT_TRUE(!in_cache[key]);
      ASSERT_TRUE(!cache_->stillInUse(key)) << ": " << key;
    }
  }
  ASSERT_TRUE(in_cache[0]);
  ASSERT_TRUE(cache_->stillInUse(0));
}

void SimpleLRUCacheTest::testRemoveUnpinned() {
  for (int i = 0; i < kCacheSize; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }

  TestValue* const val = cache_->lookup(1);
  ASSERT_TRUE(val);
  cache_->removeUnpinned();
  ASSERT_EQ(cache_->entries(), 1);
  // Check that only value 1 is still in the cache
  for (int i = 0; i < kCacheSize; i++) {
    if (i != 1) {
      ASSERT_TRUE(!in_cache[i]);
    }
  }
  ASSERT_TRUE(in_cache[1]);
  cache_->release(1, val);
  cache_->removeUnpinned();
  ASSERT_EQ(cache_->entries(), 0);
  ASSERT_TRUE(!in_cache[1]);
}

TEST_F(SimpleLRUCacheTest, RemoveUnpinned) {
  cache_.reset(new TestCache(kCacheSize));
  testRemoveUnpinned();
}

TEST_F(SimpleLRUCacheTest, RemoveUnpinnedWithIdleEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize));
  // Here we are not testing idle eviction, just that removeUnpinned
  // works correctly with it enabled.
  cache_->setMaxIdleSeconds(2000);
  testRemoveUnpinned();
}

TEST_F(SimpleLRUCacheTest, RemoveUnpinnedWithAgeBasedEvictionEnabled) {
  cache_.reset(new TestCache(kCacheSize));
  // Here we are not testing age-based eviction, just that removeUnpinned
  // works correctly with it enabled.
  cache_->setAgeBasedEviction(2000);
  testRemoveUnpinned();
}

TEST_F(SimpleLRUCacheTest, MultiInsert) {
  cache_.reset(new TestCache(kCacheSize));
  for (int i = 0; i < kElems; i++) {
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(0, v, 1);
    if (i > 0) {
      ASSERT_TRUE(!in_cache[i - 1]); // Older entry must have been evicted
    }
  }
}

TEST_F(SimpleLRUCacheTest, MultiInsertPinned) {
  cache_.reset(new TestCache(kCacheSize));
  TestValue* list[kElems];
  for (int i = 0; i < kElems; i++) {
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insertPinned(0, v, 1);
    list[i] = v;
  }
  for (int i = 0; i < kElems; i++) {
    ASSERT_TRUE(in_cache[i]);
    ASSERT_TRUE(cache_->stillInUse(0, list[i]));
  }
  for (int i = 0; i < kElems; i++) {
    cache_->release(0, list[i]);
  }
}

void SimpleLRUCacheTest::testExpiration(bool lru, bool release_quickly) {
  cache_.reset(new TestCache(kCacheSize));
  if (lru) {
    cache_->setMaxIdleSeconds(0.2); // 200 milliseconds
  } else {
    cache_->setAgeBasedEviction(0.2); // 200 milliseconds
  }
  for (int i = 0; i < kCacheSize; i++) {
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  for (int i = 0; i < kCacheSize; i++)
    ASSERT_TRUE(in_cache[i]);

  usleep(110 * 1000);

  TestValue* v1 = cache_->lookup(0);
  ASSERT_TRUE(v1 != nullptr);
  if (release_quickly) {
    cache_->release(0, v1);
    v1 = nullptr;
  }
  for (int i = 0; i < kCacheSize; i++)
    ASSERT_TRUE(in_cache[i]);

  // Sleep more: should cause expiration of everything we
  // haven't touched, and the one we touched if age-based.
  usleep(110 * 1000);

  // Nothing gets expired until we call one of the cache methods.
  for (int i = 0; i < kCacheSize; i++)
    ASSERT_TRUE(in_cache[i]);

  // It's now 220 ms since element 0 was created, and
  // 110 ms since we last looked at it. If we configured
  // the cache in LRU mode it should still be there, but
  // if we configured it in age-based mode it should be gone.
  // This is true even if the element was checked out: it should
  // be on the defer_ list, not the table_ list as it is expired.
  // Whether or not the element was pinned shouldn't matter:
  // it should be expired either way in AgeBased mode,
  // and not expired either way in lru mode.
  TestValue* v2 = cache_->lookup(0);
  ASSERT_EQ(v2 == nullptr, !lru);

  // In either case all the other elements should now be gone.
  for (int i = 1; i < kCacheSize; i++)
    ASSERT_TRUE(!in_cache[i]);

  // Clean up
  bool cleaned_up = false;
  if (v1 != nullptr) {
    cache_->release(0, v1);
    cleaned_up = true;
  }
  if (v2 != nullptr) {
    cache_->release(0, v2);
    cleaned_up = true;
  }
  if (cleaned_up) {
    cache_->remove(0);
  }
}

TEST_F(SimpleLRUCacheTest, ExpirationLRUShortHeldPins) {
  testExpiration(true /* lru */, true /* release_quickly */);
}
TEST_F(SimpleLRUCacheTest, ExpirationLRULongHeldPins) {
  testExpiration(true /* lru */, false /* release_quickly */);
}
TEST_F(SimpleLRUCacheTest, ExpirationAgeBasedShortHeldPins) {
  testExpiration(false /* lru */, true /* release_quickly */);
}
TEST_F(SimpleLRUCacheTest, ExpirationAgeBasedLongHeldPins) {
  testExpiration(false /* lru */, false /* release_quickly */);
}

void SimpleLRUCacheTest::testLargeExpiration(bool lru, double timeout) {
  // Make sure that setting a large timeout doesn't result in overflow and
  // cache entries expiring immediately.
  cache_.reset(new TestCache(kCacheSize));
  if (lru) {
    cache_->setMaxIdleSeconds(timeout);
  } else {
    cache_->setAgeBasedEviction(timeout);
  }
  for (int i = 0; i < kCacheSize; i++) {
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  for (int i = 0; i < kCacheSize; i++) {
    TestCache::ScopedLookup lookup(cache_.get(), i);
    ASSERT_TRUE(lookup.found()) << "Entry " << i << " not found";
  }
}

TEST_F(SimpleLRUCacheTest, InfiniteExpirationLRU) {
  testLargeExpiration(true /* lru */, std::numeric_limits<double>::infinity());
}

TEST_F(SimpleLRUCacheTest, InfiniteExpirationAgeBased) {
  testLargeExpiration(false /* lru */, std::numeric_limits<double>::infinity());
}

static double getBoundaryTimeout() {
  // Search for the smallest timeout value that will result in overflow when
  // converted to an integral number of cycles.
  const double seconds_to_cycles = SimpleCycleTimer::frequency();
  double seconds = static_cast<double>(std::numeric_limits<int64_t>::max()) / seconds_to_cycles;
  // Because of floating point rounding, we are not certain that the previous
  // computation will result in precisely the right value. So, jitter the value
  // until we know we found the correct value. First, look for a value that we
  // know will not result in overflow.
  while ((seconds * seconds_to_cycles) >=
         static_cast<double>(std::numeric_limits<int64_t>::max())) {
    seconds = std::nextafter(seconds, -std::numeric_limits<double>::infinity());
  }
  // Now, look for the first value that will result in overflow.
  while ((seconds * seconds_to_cycles) < static_cast<double>(std::numeric_limits<int64_t>::max())) {
    seconds = std::nextafter(seconds, std::numeric_limits<double>::infinity());
  }
  return seconds;
}

TEST_F(SimpleLRUCacheTest, LargeExpirationLRU) {
  testLargeExpiration(true /* lru */, getBoundaryTimeout());
}

TEST_F(SimpleLRUCacheTest, LargeExpirationAgeBased) {
  testLargeExpiration(false /* lru */, getBoundaryTimeout());
}

TEST_F(SimpleLRUCacheTest, UpdateSize) {
  // Create a cache larger than kCacheSize, to give us some overhead to
  // change the objects' sizes. We don't want an UpdateSize operation
  // to force a GC and throw off our ASSERT_TRUE()s down below.
  cache_.reset(new TestCache(kCacheSize * 2));
  for (int i = 0; i < kCacheSize; i++) {
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  ASSERT_EQ(cache_->entries(), kCacheSize);

  // *** Check the basic operations ***
  // We inserted kCacheSize items, each of size 1.
  // So the total should be kCacheSize, with none deferred and none pinned.

  ASSERT_EQ(cache_->size(), kCacheSize);
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 0);
  ASSERT_EQ(cache_->pinnedSize(), 0);

  // Now lock a value -- total should be the same, but one should be pinned.
  TestValue* found = cache_->lookup(0);

  ASSERT_EQ(cache_->size(), kCacheSize);
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 0);
  ASSERT_EQ(cache_->pinnedSize(), 1);

  // Now [try to] remove the locked value.
  // This should leave zero pinned, but one deferred.
  cache_->remove(0);

  ASSERT_EQ(cache_->size(), kCacheSize);
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 1);
  ASSERT_EQ(cache_->pinnedSize(), 0);

  // Now release the locked value. Both the deferred and pinned should be
  // zero, and the total size should be one less than the total before.
  cache_->release(0, found);
  found = nullptr;

  ASSERT_EQ(cache_->size(), (kCacheSize - 1));
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 0);
  ASSERT_EQ(cache_->pinnedSize(), 0);

  // *** Okay, math works. Now try changing the sizes in mid-stream. ***

  // Change one item to have a size of two. The should bring the total
  // back up to kCacheSize.
  cache_->updateSize(1, nullptr, 2);

  ASSERT_EQ(cache_->size(), kCacheSize);
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 0);
  ASSERT_EQ(cache_->pinnedSize(), 0);

  // What if we pin a value, and then change its size?

  // Pin [2]; total is still kCacheSize, pinned is one -- just like before ...
  found = cache_->lookup(2);

  ASSERT_EQ(cache_->size(), kCacheSize);
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 0);
  ASSERT_EQ(cache_->pinnedSize(), 1);

  // Update that item to be of size two ...
  cache_->updateSize(2, found, 2);

  // ... and the total should be one greater, and pinned should be two.
  ASSERT_EQ(cache_->size(), (kCacheSize + 1));
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 0);
  ASSERT_EQ(cache_->pinnedSize(), 2);

  // Okay, remove it; pinned should go to zero, Deferred should go to two.
  cache_->remove(2);

  ASSERT_EQ(cache_->size(), (kCacheSize + 1));
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 2);
  ASSERT_EQ(cache_->pinnedSize(), 0);

  // Now, change it again. Let's change it back to size one--
  // the total should go back to kCacheSize, and Deferred should
  // drop to one.
  cache_->updateSize(2, found, 1);

  ASSERT_EQ(cache_->size(), kCacheSize);
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 1);
  ASSERT_EQ(cache_->pinnedSize(), 0);

  // Release it. Total should drop by one, Deferred goes to zero.
  cache_->release(2, found);
  found = nullptr;

  ASSERT_EQ(cache_->size(), (kCacheSize - 1));
  ASSERT_EQ(cache_->maxSize(), kCacheSize * 2);
  ASSERT_EQ(cache_->deferredSize(), 0);
  ASSERT_EQ(cache_->pinnedSize(), 0);

  // So far we've disposed of 2 entries.
  ASSERT_EQ(cache_->entries(), kCacheSize - 2);

  // Now blow the cache up from the inside: resize an entry to an enormous size.
  // This will push everything out except the entry itself because it's pinned.
  TestValue* v = new TestValue(0);
  in_cache[0] = true;
  cache_->insertPinned(0, v, 1);
  ASSERT_EQ(cache_->entries(), kCacheSize - 1);
  cache_->updateSize(0, v, kCacheSize * 3);
  ASSERT_EQ(cache_->entries(), 1);
  ASSERT_EQ(cache_->size(), kCacheSize * 3);
  // The entry is disposed of as soon as it is released.
  cache_->release(0, v);
  ASSERT_EQ(cache_->entries(), 0);
  ASSERT_EQ(cache_->size(), 0);
}

TEST_F(SimpleLRUCacheTest, DontUpdateEvictionOrder) {
  cache_.reset(new TestCache(kCacheSize));
  int64_t original_start, original_end;

  SimpleLRUCacheOptions options;
  options.set_update_eviction_order(false);

  // Fully populate the cache and keep track of the time range for this
  // population.
  original_start = SimpleCycleTimer::now();
  tickClock();
  for (int i = 0; i < kCacheSize; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    cache_->insert(i, new TestValue(i), 1);
    in_cache[i] = true;
  }
  tickClock();
  original_end = SimpleCycleTimer::now();

  // At each step validate the current state of the cache and then insert
  // a new element.
  for (int step = 0; step < kElems - kCacheSize; ++step) {
    // Look from end to beginning (the reverse the order of insertion). This
    // makes sure nothing changes cache ordering.
    for (int this_elem = kElems - 1; this_elem >= 0; this_elem--) {
      if (!in_cache[this_elem]) {
        ASSERT_EQ(-1, cache_->getLastUseTime(this_elem));
      } else if (this_elem < kCacheSize) {
        // All elements < kCacheSize were part of the original insertion.
        ASSERT_GT(cache_->getLastUseTime(this_elem), original_start);
        ASSERT_LT(cache_->getLastUseTime(this_elem), original_end);
      } else {
        // All elements >= kCacheSize are newer.
        ASSERT_GT(cache_->getLastUseTime(this_elem), original_end);
      }

      TestValue* value = cache_->lookupWithOptions(this_elem, options);
      TestCache::ScopedLookup scoped_lookup(cache_.get(), this_elem, options);
      if (in_cache[this_elem]) {
        ASSERT_TRUE(value != nullptr);
        ASSERT_EQ(this_elem, value->label);
        ASSERT_TRUE(scoped_lookup.value() != nullptr);
        ASSERT_EQ(this_elem, scoped_lookup.value()->label);
        cache_->releaseWithOptions(this_elem, value, options);
      } else {
        ASSERT_TRUE(value == nullptr);
        ASSERT_TRUE(scoped_lookup.value() == nullptr);
      }
    }

    // insert TestValue(kCacheSize + step) which should evict the TestValue with
    // label step.
    cache_->insert(kCacheSize + step, new TestValue(kCacheSize + step), 1);
    in_cache[kCacheSize + step] = true;
    in_cache[step] = false;
  }
}

TEST_F(SimpleLRUCacheTest, ScopedLookup) {
  cache_.reset(new TestCache(kElems));
  for (int i = 0; i < kElems; i++) {
    ASSERT_TRUE(!cache_->lookup(i));
    TestValue* v = new TestValue(i);
    in_cache[i] = true;
    cache_->insert(i, v, 1);
  }
  ASSERT_EQ(cache_->pinnedSize(), 0);
  {
    typedef TestCache::ScopedLookup ScopedLookup;
    // Test two successful lookups
    ScopedLookup lookup1(cache_.get(), 1);
    ASSERT_TRUE(lookup1.found());
    ASSERT_EQ(cache_->pinnedSize(), 1);

    ScopedLookup lookup2(cache_.get(), 2);
    ASSERT_TRUE(lookup2.found());
    ASSERT_EQ(cache_->pinnedSize(), 2);

    // Test a lookup of an elem not in the cache.
    ScopedLookup lookup3(cache_.get(), kElems + 1);
    ASSERT_TRUE(!lookup3.found());
    ASSERT_EQ(cache_->pinnedSize(), 2);
  }
  // Make sure the destructors released properly.
  ASSERT_EQ(cache_->pinnedSize(), 0);
}

TEST_F(SimpleLRUCacheTest, AgeOfLRUItemInMicroseconds) {
  // Make sure empty cache returns zero.
  cache_.reset(new TestCache(kElems));
  ASSERT_EQ(cache_->ageOfLRUItemInMicroseconds(), 0);

  // Make sure non-empty cache doesn't return zero.
  TestValue* v = new TestValue(1);
  in_cache[1] = true;
  cache_->insert(1, v, 1);
  tickClock(); // must let at least 1us go by
  ASSERT_NE(cache_->ageOfLRUItemInMicroseconds(), 0);

  // Make sure "oldest" ages as time goes by.
  int64_t oldest = cache_->ageOfLRUItemInMicroseconds();
  tickClock();
  ASSERT_GT(cache_->ageOfLRUItemInMicroseconds(), oldest);

  // Make sure new addition doesn't count as "oldest".
  oldest = cache_->ageOfLRUItemInMicroseconds();
  tickClock();
  v = new TestValue(2);
  in_cache[2] = true;
  cache_->insert(2, v, 1);
  ASSERT_GT(cache_->ageOfLRUItemInMicroseconds(), oldest);

  // Make sure removal of oldest drops to next oldest.
  oldest = cache_->ageOfLRUItemInMicroseconds();
  cache_->remove(1);
  ASSERT_LT(cache_->ageOfLRUItemInMicroseconds(), oldest);

  // Make sure that empty cache one again returns zero.
  cache_->remove(2);
  tickClock();
  ASSERT_EQ(cache_->ageOfLRUItemInMicroseconds(), 0);
}

TEST_F(SimpleLRUCacheTest, GetLastUseTime) {
  cache_.reset(new TestCache(kElems));
  int64_t now, last;

  // Make sure nonexistent key returns -1
  ASSERT_EQ(cache_->getLastUseTime(1), -1);

  // Make sure existent key returns something > last and < now
  last = SimpleCycleTimer::now();
  tickClock();
  in_cache[1] = true;
  TestValue* v = new TestValue(1);
  cache_->insert(1, v, 1);
  tickClock();
  now = SimpleCycleTimer::now();
  ASSERT_GT(cache_->getLastUseTime(1), last);
  ASSERT_LT(cache_->getLastUseTime(1), now);

  // Make sure next element > stored time and < now
  in_cache[2] = true;
  v = new TestValue(2);
  cache_->insert(2, v, 1);
  tickClock();
  now = SimpleCycleTimer::now();
  ASSERT_GT(cache_->getLastUseTime(2), cache_->getLastUseTime(1));
  ASSERT_LT(cache_->getLastUseTime(2), now);

  // Make sure last use doesn't change after lookup
  last = cache_->getLastUseTime(1);
  v = cache_->lookup(1);
  ASSERT_EQ(cache_->getLastUseTime(1), last);

  // Make sure last use changes after release, and is > last use of 2 < now
  tickClock();
  cache_->release(1, v);
  tickClock();
  now = SimpleCycleTimer::now();
  ASSERT_GT(cache_->getLastUseTime(1), cache_->getLastUseTime(2));
  ASSERT_LT(cache_->getLastUseTime(1), now);

  // Make sure insert updates last use, > last use of 1 < now
  v = new TestValue(3);
  cache_->insert(2, v, 1);
  in_cache[3] = true;
  tickClock();
  now = SimpleCycleTimer::now();
  ASSERT_GT(cache_->getLastUseTime(2), cache_->getLastUseTime(1));
  ASSERT_LT(cache_->getLastUseTime(2), now);

  // Make sure iterator returns the same value as getLastUseTime
  for (TestCache::const_iterator it = cache_->begin(); it != cache_->end(); ++it) {
    ASSERT_EQ(it.last_use_time(), cache_->getLastUseTime(it->first));
  }

  // Make sure after remove returns -1
  cache_->remove(2);
  ASSERT_EQ(cache_->getLastUseTime(2), -1);
}

TEST_F(SimpleLRUCacheTest, GetInsertionTime) {
  cache_.reset(new TestCache(kElems));
  int64_t now, last;

  cache_->setAgeBasedEviction(-1);

  // Make sure nonexistent key returns -1
  ASSERT_EQ(cache_->getInsertionTime(1), -1);

  // Make sure existent key returns something > last and < now
  last = SimpleCycleTimer::now();
  tickClock();
  in_cache[1] = true;
  TestValue* v = new TestValue(1);
  cache_->insert(1, v, 1);
  tickClock();
  now = SimpleCycleTimer::now();
  ASSERT_GT(cache_->getInsertionTime(1), last);
  ASSERT_LT(cache_->getInsertionTime(1), now);

  // Make sure next element > time of element 1 and < now
  in_cache[2] = true;
  v = new TestValue(2);
  cache_->insert(2, v, 1);
  tickClock();
  now = SimpleCycleTimer::now();
  ASSERT_GT(cache_->getInsertionTime(2), cache_->getInsertionTime(1));
  ASSERT_LT(cache_->getInsertionTime(2), now);

  // Make sure insertion time doesn't change after lookup
  last = cache_->getInsertionTime(1);
  v = cache_->lookup(1);
  ASSERT_EQ(cache_->getInsertionTime(1), last);

  // Make sure insertion time doesn't change after release
  tickClock();
  cache_->release(1, v);
  ASSERT_EQ(cache_->getInsertionTime(1), last);

  // Make sure insert updates time, > insertion time of 2 < now
  in_cache[3] = true;
  v = new TestValue(3);
  cache_->insert(1, v, 1);
  tickClock();
  now = SimpleCycleTimer::now();
  ASSERT_GT(cache_->getInsertionTime(1), cache_->getInsertionTime(2));
  ASSERT_LT(cache_->getInsertionTime(1), now);

  // Make sure iterator returns the same value as getInsertionTime
  for (TestCache::const_iterator it = cache_->begin(); it != cache_->end(); ++it) {
    ASSERT_EQ(it.insertion_time(), cache_->getInsertionTime(it->first));
  }

  // Make sure after remove returns -1
  cache_->remove(2);
  ASSERT_EQ(cache_->getInsertionTime(2), -1);
}

std::string StringPrintf(void* p, int pin, int defer) {
  std::stringstream ss;
  ss << std::hex << p << std::dec << ": pin: " << pin;
  ss << ", is_deferred: " << defer;
  return ss.str();
}

TEST_F(SimpleLRUCacheTest, DebugOutput) {
  cache_.reset(new TestCache(kCacheSize, false /* check_in_cache */));
  TestValue* v1 = new TestValue(0);
  cache_->insertPinned(0, v1, 1);
  TestValue* v2 = new TestValue(0);
  cache_->insertPinned(0, v2, 1);
  TestValue* v3 = new TestValue(0);
  cache_->insert(0, v3, 1);

  std::string s;
  cache_->debugOutput(&s);
  EXPECT_THAT(s, HasSubstr(StringPrintf(v1, 1, 1)));
  EXPECT_THAT(s, HasSubstr(StringPrintf(v2, 1, 1)));
  EXPECT_THAT(s, HasSubstr(StringPrintf(v3, 0, 0)));

  cache_->release(0, v1);
  cache_->release(0, v2);
}

TEST_F(SimpleLRUCacheTest, LookupWithoutEvictionOrderUpdateAndRemove) {
  cache_.reset(new TestCache(kCacheSize, false /* check_in_cache */));

  for (int i = 0; i < 3; ++i) {
    cache_->insert(i, new TestValue(0), 1);
  }

  SimpleLRUCacheOptions no_update_options;
  no_update_options.set_update_eviction_order(false);
  TestValue* value = cache_->lookupWithOptions(1, no_update_options);
  // Remove the second element before calling releaseWithOptions. Since we used
  // update_eviction_order = false for the LookupWithOptions call the value was
  // not removed from the LRU. remove() is responsible for taking the value out
  // of the LRU.
  cache_->remove(1);
  // releaseWithOptions will now delete the pinned value.
  cache_->releaseWithOptions(1, value, no_update_options);

  // When using ASan these lookups verify that the LRU has not been corrupted.
  EXPECT_THAT(TestCache::ScopedLookup(cache_.get(), 0).value(), NotNull());
  EXPECT_THAT(TestCache::ScopedLookup(cache_.get(), 2).value(), NotNull());
}

} // namespace SimpleLruCache
} // namespace Envoy
