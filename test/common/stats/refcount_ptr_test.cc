#include <string>
#include <vector>

#include "envoy/stats/refcount_ptr.h"

#include "test/test_common/thread_factory_for_test.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class RefcountedString : public std::string, public RefcountHelper {
public:
  explicit RefcountedString(const std::string& s) : std::string(s) {}
};
using SharedString = RefcountPtr<RefcountedString>;

class DerivedRefcountedString : public RefcountedString {};
using DerivedSharedString = RefcountPtr<RefcountedString>;

TEST(RefcountPtr, Constructors) {
  SharedString rp1; // Default constructor.
  EXPECT_FALSE(rp1);
  rp1 = new RefcountedString("Hello"); // Assign from pointer.
  EXPECT_EQ(1, rp1.use_count());
  SharedString rp2(rp1); // Copy-constructor.
  EXPECT_EQ(2, rp1.use_count());
  EXPECT_EQ(2, rp2.use_count());
  EXPECT_EQ(rp1, rp2);
  EXPECT_EQ(*rp1, *rp2);
  *rp1 += ", World!"; // Object is shared, so mutations are shared.
  EXPECT_EQ(rp1, rp2);
  EXPECT_EQ(*rp1, *rp2);
  EXPECT_EQ("Hello, World!", *rp2);
  SharedString rp3(std::move(rp2)); // Move-constructor.
  EXPECT_EQ(2, rp3.use_count());
  EXPECT_EQ("Hello, World!", *rp3);
  EXPECT_NE(rp2, rp3);     // NOLINT -- intentionally testing what happens to a variable post-move.
  EXPECT_EQ(nullptr, rp2); // NOLINT -- ditto
  EXPECT_NE(rp1, rp2);     // NOLINT -- ditto
  EXPECT_EQ(rp1, rp3);
  EXPECT_FALSE(rp2); // NOLINT -- ditto
  EXPECT_TRUE(rp3);
  EXPECT_TRUE(rp1);
  SharedString rp4(new RefcountedString("Hello, World!")); // Construct from pointer.
  EXPECT_EQ(*rp4, *rp3);
  EXPECT_NE(rp4, rp3);
  DerivedSharedString rp5(rp4); // Construct across hierarchies.
  EXPECT_EQ(rp5, rp4);
  EXPECT_EQ(*rp5, *rp4);
  SharedString rp6;
  rp6 = std::move(rp4);    // move-assign.
  EXPECT_EQ(nullptr, rp4); // NOLINT -- intentionally testing what happens to a variable post-move.
  EXPECT_EQ(rp5, rp6);
}

TEST(RefcountPtr, Operators) {
  RefcountedString* ptr = new RefcountedString("Hello, World!");
  SharedString shared(ptr);
  EXPECT_TRUE(shared);
  EXPECT_EQ(13, shared->size());
  RefcountedString& ref = *shared;
  EXPECT_EQ(&ref, ptr);
  SharedString shared2(new RefcountedString("Hello, World!"));
  EXPECT_NE(&ref, shared2.get());
  SharedString shared3(shared2);
  EXPECT_EQ(shared2, shared3);
  EXPECT_EQ(2, shared2.use_count());
  shared2.reset();
  EXPECT_EQ(nullptr, shared2);
  EXPECT_EQ(1, shared3.use_count());
}

// Reads through references from many threads while the last of them to drop
// its reference deletes the object; every read must happen-before the delete.
// The copies are made sequentially on the main thread (capture-by-value at
// thread creation), so only reads, releases, and the deletion race with each
// other. Meaningful primarily under TSAN, which verifies the release/acquire
// pairing on RefcountHelper's reference count.
TEST(RefcountPtr, ThreadedCopyDropChurn) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  const uint32_t num_threads = 8;
  const uint32_t iters = 500;
  for (uint32_t iter = 0; iter < iters; ++iter) {
    SharedString shared(new RefcountedString("Hello"));
    absl::Notification go;
    std::vector<Thread::ThreadPtr> threads;
    for (uint32_t i = 0; i < num_threads; ++i) {
      threads.push_back(thread_factory.createThread([copy = SharedString(shared), &go]() mutable {
        go.WaitForNotification();
        EXPECT_EQ(5, copy->size());
        copy.reset(); // Possibly the final release, which deletes the string.
      }));
    }
    shared.reset(); // Drop the main thread's reference before releasing the readers.
    go.Notify();
    for (uint32_t i = 0; i < num_threads; ++i) {
      threads[i]->join();
    }
  }
}

} // namespace Stats
} // namespace Envoy
