#include <array>

#include "common/common/opaque_scope_tracker.h"
#include "common/common/utility.h"

#include "envoy/common/scope_tracker.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include <functional>

namespace Envoy {
namespace {

TEST(OpaqueScopeTrackedObjectTest, ShouldDumpTrackedObjectsInFILO) {
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  MessageTrackedObject first{"first"};
  MessageTrackedObject second{"second"};
  std::vector<std::reference_wrapper<const ScopeTrackedObject>> tracked_objects{first, second};

  OpaqueScopeTrackedObject opaque_tracker{std::move(tracked_objects)};
  opaque_tracker.dumpState(ostream, 0);

  EXPECT_EQ(ostream.contents(), "secondfirst");
}

} // namespace
} // namespace Envoy
