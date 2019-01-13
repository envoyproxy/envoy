#include "common/common/phantom.h"

#include "gtest/gtest.h"

namespace Envoy {

// Verify that we're able to initialize a class with explict ctor.
struct PhantomA {
  explicit PhantomA(uint32_t x) : x_(x) {}

  bool operator==(const PhantomA& other) { return x_ == other.x_; }

  bool operator==(const PhantomA& other) const { return x_ == other.x_; }

  uint32_t x_;
};

// Verify that we're able to initialize a class with implicit ctor.
struct PhantomB {
  PhantomB(uint32_t x) : x_(x) {}

  bool operator==(const PhantomB& other) { return x_ == other.x_; }

  bool operator==(const PhantomB& other) const { return x_ == other.x_; }

  uint32_t x_;
};

// Empty types to use as phantom type.
struct PhantomTest {};
struct PhantomTest2 {};

typedef Phantom<PhantomA, PhantomTest> PhantomATest;
typedef Phantom<PhantomA, PhantomTest2> PhantomATest2;
typedef Phantom<PhantomB, PhantomTest> PhantomBTest;
typedef Phantom<PhantomB, PhantomTest2> PhantomBTest2;

typedef Phantom<std::vector<uint32_t>, PhantomTest> PhantomVector;
typedef Phantom<PhantomVector, PhantomTest> NestedPhantomVector;

// Helper functions for testing type interaction with nested phantoms.
void parent(PhantomVector) {}
void parentRef(const PhantomVector&) {}

void base(NestedPhantomVector) {}
void baseRef(const NestedPhantomVector&) {}

// Verify that a phantom type can be constructed using the inner
// type's constructors
TEST(PhantomTest, TypeBehavior) {
  {
    const auto x = PhantomATest::create(4);
    const auto y = PhantomATest::create(4);
    /* Phantom<PhantomA, struct PhantomTest> x{4}; */
    /* Phantom<PhantomA, struct PhantomTest> y{4}; */

    // Equality is provided by the super class.
    EXPECT_EQ(x, y);
    // Phantom should be convertible to the inner type.
    EXPECT_EQ(PhantomA(x), PhantomA(4));
  }

  {
    const auto x = PhantomBTest::create(4);
    const auto y = PhantomBTest::create(4);

    // Equality is provided by the super class.
    EXPECT_EQ(x, y);
    // Phantom should be convertible to the inner type.
    EXPECT_EQ(PhantomB(x), 4u);
  }

  {
    auto x = PhantomATest::create(4);
    const auto y = PhantomATest2::create(4);

    // Should not be possible to convert x to y directly.
    static_assert(!std::is_convertible<decltype(x), decltype(y)>::value, "not convertible");
    static_assert(!std::is_assignable<decltype(x), decltype(y)>::value, "not assignable");

    // Explicit conversion should be possible.
    x = Phantom<PhantomA, struct PhantomTest>::create(y);
  }

  {
    // Verify initializer list initialization of a vector.
    const auto v = PhantomVector::create({1u, 2u, 3u, 4u});
  }

  {
    // Verify that initializer syntax is preferred over size_t, const T& ctor.
    const auto v = PhantomVector::create({1u, 2u});
    const auto v2 = PhantomVector::create(std::vector<uint32_t>({1u, 2u}));

    EXPECT_EQ(v, v2);
  }

  {
    const auto nested = NestedPhantomVector::create({1u, 2u});
    const auto nested2 = NestedPhantomVector::create({1u, 2u});

    EXPECT_EQ(nested, nested2);

    // Passing the nested type to a function that takes the inner phantom works due to inheritence.
    // We know we're not doing a copy here because all the ctors are explicit.
    parent(nested);
    parentRef(nested);

    base(nested);
    baseRef(nested);

    const auto inner = PhantomVector::create();

    // TODO(snowp): Need C++17 for these, but would be nice to have.
    // static_assert(!std::is_invokable<decltype(base), decltype(inner)>::value,
    //   "cannot pass inner to parent func");
    //static_assert(!std::is_invokable<decltype(baseRef), decltype(inner)>::value,
    //  "cannot pass inner to parent func");
  }

  // Verify that default initialization works.
  NestedPhantomVector v;
}

} // namespace Envoy
