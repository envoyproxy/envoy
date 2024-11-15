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
  EXPECT_DEATH({ DangerousDeprecatedTestTime t2; },
               ".*Two different types of time-systems allocated.*");
}

TEST_F(TestTimeSystemTest, SimThenRealSerial) {
  { SimulatedTimeSystem t1; }
  { DangerousDeprecatedTestTime t2; }
}

TEST_F(TestTimeSystemTest, RealThenSim) {
  DangerousDeprecatedTestTime t1;
  EXPECT_DEATH({ SimulatedTimeSystem t2; }, ".*Two different types of time-systems allocated.*");
}

TEST_F(TestTimeSystemTest, RealThenSimSerial) {
  { DangerousDeprecatedTestTime t2; }
  { SimulatedTimeSystem t1; }
}

} // namespace
} // namespace Test
} // namespace Event
} // namespace Envoy
