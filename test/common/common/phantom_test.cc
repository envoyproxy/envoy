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

// Helper functions for testing type interaction with nested phantoms.
void parent(Phantom<std::vector<uint32_t>, struct PhantomTest>) {}
void parentRef(const Phantom<std::vector<uint32_t>, struct PhantomTest>&) {}

void base(Phantom<Phantom<std::vector<uint32_t>, struct PhantomTest>, struct PhantomTest2>) {}
void baseRef(
    const Phantom<Phantom<std::vector<uint32_t>, struct PhantomTest>, struct PhantomTest2>&) {}

// Verify that a phantom type can be constructed using the inner
// type's constructors
TEST(PhantomTest, TypeBehavior) {
  {
    const auto x = Phantom<PhantomA, struct PhantomTest>::create(4);
    const auto y = Phantom<PhantomA, struct PhantomTest>::create(4);
    /* Phantom<PhantomA, struct PhantomTest> x{4}; */
    /* Phantom<PhantomA, struct PhantomTest> y{4}; */

    // Equality is provided by the super class.
    EXPECT_EQ(x, y);
    // Phantom should be convertible to the inner type.
    EXPECT_EQ(PhantomA(x), PhantomA(4));
  }

  {
    const auto x = Phantom<PhantomB, struct PhantomTest>::create(4);
    const auto y = Phantom<PhantomB, struct PhantomTest>::create(4);

    // Equality is provided by the super class.
    EXPECT_EQ(x, y);
    // Phantom should be convertible to the inner type.
    EXPECT_EQ(PhantomB(x), 4u);
  }

  {
    auto x = Phantom<PhantomA, struct PhantomTest>::create(4);
    const auto y = Phantom<PhantomA, struct PhantomTest2>::create(4);

    // Should not be possible to convert x to y directly.
    static_assert(!std::is_convertible<decltype(x), decltype(y)>::value, "not convertible");
    static_assert(!std::is_assignable<decltype(x), decltype(y)>::value, "not assignable");

    // Explicit conversion should be possible.
    x = Phantom<PhantomA, struct PhantomTest>::create(y);
  }

  {
    // Verify initializer list initialization of a vector.
    /* Phantom<std::vector<uint32_t>, struct PhantomTest2> v({1u, 2u, 3u, 4u}); */
    /* Phantom<std::vector<uint32_t>, struct PhantomTest2> v2{1u, 2u, 3u, 4u}; */
    const auto v = Phantom<std::vector<uint32_t>, struct PhantomTest>::create({1u, 2u, 3u, 4u});
    const auto v2 = Phantom<std::vector<uint32_t>, struct PhantomTest>::create({1u, 2u, 3u, 4u});

    EXPECT_EQ(v, v2);
  }

  {
    // Verify that initializer syntax is preferred over size_t, const T& ctor
    /* Phantom<std::vector<uint32_t>, struct PhantomTest2> v{1u, 2u}; */
    /* Phantom<std::vector<uint32_t>, struct PhantomTest2> v2({1u, 2u}); */
    const auto v = Phantom<std::vector<uint32_t>, struct PhantomTest>::create({1u, 2u});
    const auto v2 = Phantom<std::vector<uint32_t>, struct PhantomTest>::create({1u, 2u});

    EXPECT_EQ(v, v2);
  }

  {
    const auto nested =
        Phantom<Phantom<std::vector<uint32_t>, struct PhantomTest>, struct PhantomTest2>::create(
            {1u, 2u});
    const auto nested2 =
        Phantom<Phantom<std::vector<uint32_t>, struct PhantomTest>, struct PhantomTest2>::create(
            {1u, 2u});

    EXPECT_EQ(nested, nested2);

    // Passing the nested type to a function that takes the inner phantom works due to inheritence.
    // We know we're not doing a copy here because all the ctors are explicit.
    parent(nested);
    parentRef(nested);

    base(nested);
    baseRef(nested);

    const auto inner = Phantom<std::vector<uint32_t>, struct PhantomTest>::create();

    static_assert(!std::__invokable<decltype(base), decltype(inner)>::value,
                  "cannot pass inner to parent func");
    static_assert(!std::__invokable<decltype(baseRef), decltype(inner)>::value,
                  "cannot pass inner to parent func");
  }
}

} // namespace Envoy
