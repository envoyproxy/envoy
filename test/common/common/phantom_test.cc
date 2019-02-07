#include "common/common/phantom.h"

#include "test/test_common/test_base.h"

namespace Envoy {

struct PhantomTest1 {};
struct PhantomTest2 {};

typedef Phantom<uint32_t, PhantomTest1> PhantomIntTest1;
typedef Phantom<uint32_t, PhantomTest2> PhantomIntTest2;

using PhantomTest = TestBase;

TEST_F(PhantomTest, TypeBehavior) {
  // Should not be possible to implicitly convert from two phantoms with different markers.
  static_assert(!std::is_convertible<PhantomIntTest1, PhantomTest2>::value,
                "should not be convertible");
}

} // namespace Envoy
