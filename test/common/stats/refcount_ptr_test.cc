#include <string>

#include "envoy/stats/refcount_ptr.h"

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
  SharedString shared3(shared2.get());
  EXPECT_EQ(shared2, shared3);
  EXPECT_EQ(2, shared2.use_count());
  shared2.reset();
  EXPECT_EQ(nullptr, shared2);
  EXPECT_EQ(1, shared3.use_count());
}

} // namespace Stats
} // namespace Envoy
