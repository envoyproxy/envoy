#include <thread>

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace Test {
namespace {

class TestTimeSystemTest : public testing::Test {
protected:
};

TEST_F(TestTimeSystemTest, NonzeroMonotonicEpoch) {
  SimulatedTimeSystem time_system;
  EXPECT_GT(time_system.monotonicTime(), MonotonicTime{});
}

TEST_F(TestTimeSystemTest, IoDispatcher) {
  SimulatedTimeSystem time_system;
  GlobalTimeSystem global_time_system;
  Api::ApiPtr api = Api::createApiForTest(time_system);
  auto dispatcher = api->allocateDispatcher("test_thread");
  const MonotonicTime start = time_system.monotonicTime();
  global_time_system.advanceTimeForIo(*dispatcher);
  EXPECT_EQ(start, time_system.monotonicTime());

  global_time_system.setIoDispatcher(dispatcher.get());
  EXPECT_EQ(dispatcher.get(), time_system.ioDispatcher());
  bool fired = false;
  auto timer = dispatcher->createTimer([&fired] { fired = true; });
  timer->enableTimer(std::chrono::milliseconds(5));
  TestTimeSystem::RealTimeBound bound(std::chrono::seconds(5) * TIMEOUT_FACTOR);
  while (!fired && bound.withinBound()) {
    global_time_system.advanceTimeForIo(*dispatcher);
    dispatcher->run(Dispatcher::RunType::NonBlock);
    std::this_thread::yield();
  }
  EXPECT_TRUE(fired) << "Timed out waiting for the I/O dispatcher alarm";

  const MonotonicTime after_io = time_system.monotonicTime();
  time_system.setIoDispatcher(nullptr);
  global_time_system.advanceTimeForIo(*dispatcher);
  EXPECT_EQ(after_io, time_system.monotonicTime());
}

TEST_F(TestTimeSystemTest, TwoSimsSameReference) {
  SimulatedTimeSystem t1, t2;
  EXPECT_EQ(&t1.timeSystem(), &t2.timeSystem());
}

TEST_F(TestTimeSystemTest, TwoRealsSameReference) {
  DangerousDeprecatedTestTime t1, t2;
  EXPECT_EQ(&t1.timeSystem(), &t2.timeSystem());
}

TEST_F(TestTimeSystemTest, SimThenRealConflict) {
  SimulatedTimeSystem t1;
  EXPECT_DEATH(
      { DangerousDeprecatedTestTime t2; }, ".*Two different types of time-systems allocated.*");
}

TEST_F(TestTimeSystemTest, SimThenRealSerial) {
  {
    SimulatedTimeSystem t1;
  }
  {
    DangerousDeprecatedTestTime t2;
  }
}

TEST_F(TestTimeSystemTest, RealThenSim) {
  DangerousDeprecatedTestTime t1;
  EXPECT_DEATH({ SimulatedTimeSystem t2; }, ".*Two different types of time-systems allocated.*");
}

TEST_F(TestTimeSystemTest, RealThenSimSerial) {
  {
    DangerousDeprecatedTestTime t2;
  }
  {
    SimulatedTimeSystem t1;
  }
}

} // namespace
} // namespace Test
} // namespace Event
} // namespace Envoy
