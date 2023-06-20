#include <array>

#include "envoy/common/scope_tracker.h"

#include "source/common/common/scope_tracked_object_stack.h"
#include "source/common/common/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(OpaqueScopeTrackedObjectTest, ShouldDumpTrackedObjectsInFILO) {
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  MessageTrackedObject first{"first"};
  MessageTrackedObject second{"second"};

  ScopeTrackedObjectStack encapsulated_object;
  encapsulated_object.add(first);
  encapsulated_object.add(second);
  encapsulated_object.dumpState(ostream, 0);

  EXPECT_EQ(ostream.contents(), "secondfirst");
}

} // namespace
} // namespace Envoy
