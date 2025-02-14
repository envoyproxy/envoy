#include <array>

#include "envoy/common/scope_tracker.h"

#include "source/common/common/scope_tracked_object_stack.h"
#include "source/common/common/utility.h"

#include "test/mocks/stream_info/mocks.h"
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

TEST(OpaqueScopeTrackedObjectTest, ReturnTrackedStreamInFILO) {
  StreamInfo::MockStreamInfo first_stream_info;
  StreamInfo::MockStreamInfo second_stream_info;
  MessageTrackedObject first{"first", first_stream_info};
  MessageTrackedObject second{"second", second_stream_info};

  ScopeTrackedObjectStack encapsulated_object;
  encapsulated_object.add(first);
  encapsulated_object.add(second);

  EXPECT_EQ(encapsulated_object.trackedStream().ptr(), &second_stream_info);
}

} // namespace
} // namespace Envoy
