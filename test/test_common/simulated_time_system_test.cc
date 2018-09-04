#include "test/test_common/simulated_time_system.h"

#include "common/event/libevent.h"

#include "event2/event.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace Test {

class SimulatedTimeSystemTest : public testing::Test {
 protected:
  SimulatedTimeSystemTest()
      : event_system_1_(event_base_new()),
        scheduler_1_(sim_.createScheduler(event_system_1_)),
        event_system_2_(event_base_new()),
        scheduler_2_(sim_.createScheduler(event_system_2_)) {}

  void addTask(SchedulerPtr& scheduler, int64_t delay_ms, char marker) {
    TimerPtr timer = scheduler->createTimer([this, marker]() { output_.append(1, marker); });
    std::chrono::milliseconds d(delay_ms);
    timer->enableTimer(d);
    timers_.push_back(std::move(timer));
  }

  void sleepAndRunSchedule1(int64_t delay_ms) {
    sim_.sleep(std::chrono::milliseconds(delay_ms));
    event_base_loop(event_system_1_.get(), EVLOOP_NONBLOCK);
  }

  SimulatedTimeSystem sim_;
  Libevent::BasePtr event_system_1_;
  SchedulerPtr scheduler_1_;
  Libevent::BasePtr event_system_2_;
  SchedulerPtr scheduler_2_;
  std::string output_;
  std::vector<TimerPtr> timers_;
};

TEST_F(SimulatedTimeSystemTest, Ordering) {
  addTask(scheduler_1_, 5, '5');
  addTask(scheduler_1_, 3, '3');
  addTask(scheduler_1_, 6, '6');
  EXPECT_EQ("", output_);
  sleepAndRunSchedule1(5);
  EXPECT_EQ("35", output_);
  sleepAndRunSchedule1(1);
  EXPECT_EQ("356", output_);
}

TEST_F(SimulatedTimeSystemTest, Disable) {
  addTask(scheduler_1_, 5, '5');
  addTask(scheduler_1_, 3, '3');
  addTask(scheduler_1_, 6, '6');
  timers_[0]->disableTimer();
  EXPECT_EQ("", output_);
  sleepAndRunSchedule1(5);
  EXPECT_EQ("3", output_);
  sleepAndRunSchedule1(1);
  EXPECT_EQ("36", output_);
}

TEST_F(SimulatedTimeSystemTest, TwoIndependentSchedulers) {
  addTask(scheduler_1_, 5, '5');
  addTask(scheduler_2_, 3, '3');
  addTask(scheduler_2_, 6, '6');
  EXPECT_EQ("", output_);
  sleepAndRunSchedule1(5);
  EXPECT_EQ("5", output_);
  sleepAndRunSchedule1(1);
  EXPECT_EQ("5", output_);
  event_base_loop(event_system_2_.get(), EVLOOP_NONBLOCK);
  EXPECT_EQ("536", output_);

}

} // namespace Test
} // namespace Event
} // namespace Envoy
