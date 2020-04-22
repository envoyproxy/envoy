#include "common/common/phantom.h"

#include "gtest/gtest.h"

namespace Envoy {

struct PhantomTest {};
struct PhantomTest2 {};

using PhantomIntTest = Phantom<uint32_t, PhantomTest>;
using PhantomIntTest2 = Phantom<uint32_t, PhantomTest2>;

TEST(PhantomTest, TypeBehavior) {
  // Should not be possible to implicitly convert from two phantoms with different markers.
  static_assert(!std::is_convertible<PhantomIntTest, PhantomTest2>::value,
                "should not be convertible");
}

} // namespace Envoy
