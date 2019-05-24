#include "common/event/libevent_scheduler.h"

#include "extensions/quic_listeners/quiche/envoy_quic_alarm.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Event::Dispatcher;
using quic::QuicTime;

namespace Envoy {
namespace Quic {

class TestDelegate : public quic::QuicAlarm::Delegate {
public:
  TestDelegate() : fired_(false) {}

  void OnAlarm() override { fired_ = true; }
  bool fired() const { return fired_; }

private:
  bool fired_;
};

class EnvoyQuicAlarmTest : public ::testing::Test {
public:
  EnvoyQuicAlarmTest()
      : clock_(time_system_), scheduler_(time_system_.createScheduler(base_scheduler_)),
        alarm_factory_(*scheduler_, clock_) {}

  void advanceUsAndLoop(int64_t delay_us) {
    time_system_.sleep(std::chrono::microseconds(delay_us));
    base_scheduler_.run(Dispatcher::RunType::NonBlock);
  }

protected:
  Event::SimulatedTimeSystemHelper time_system_;
  EnvoyQuicClock clock_;
  Event::LibeventScheduler base_scheduler_;
  Event::SchedulerPtr scheduler_;
  EnvoyQuicAlarmFactory alarm_factory_;
  quic::QuicConnectionArena arena_;
};

TEST_F(EnvoyQuicAlarmTest, CreateAlarmByFactory) {
  auto unowned_delegate = new TestDelegate();
  quic::QuicAlarm* alarm = alarm_factory_.CreateAlarm(unowned_delegate);
  alarm->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(10));
  // Advance 9us, alarm shouldn't fire.
  advanceUsAndLoop(9);
  EXPECT_FALSE(unowned_delegate->fired());
  // Advance 1us, alarm should have fired.
  advanceUsAndLoop(1);
  EXPECT_TRUE(unowned_delegate->fired());
  delete alarm;

  unowned_delegate = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm_ptr = alarm_factory_.CreateAlarm(
      quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate>(unowned_delegate), &arena_);
  EXPECT_FALSE(alarm_ptr->IsSet());
  unowned_delegate = new TestDelegate();
  alarm_ptr = alarm_factory_.CreateAlarm(
      quic::QuicArenaScopedPtr<quic::QuicAlarm::Delegate>(unowned_delegate), nullptr);
  EXPECT_FALSE(alarm_ptr->IsSet());
}

TEST_F(EnvoyQuicAlarmTest, CreateAlarmAndCancel) {
  auto unowned_delegate1 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm1(alarm_factory_.CreateAlarm(unowned_delegate1));
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(10));
  EXPECT_TRUE(alarm1->IsSet());
  auto unowned_delegate2 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm2(alarm_factory_.CreateAlarm(unowned_delegate2));
  alarm2->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(10));
  EXPECT_TRUE(alarm2->IsSet());

  alarm1->Cancel();
  EXPECT_FALSE(alarm1->IsSet());
  // Advance 10us, alarm1 shouldn't fire.
  advanceUsAndLoop(10);
  EXPECT_TRUE(unowned_delegate2->fired());
  EXPECT_FALSE(unowned_delegate1->fired());
}

TEST_F(EnvoyQuicAlarmTest, CreateAlarmAndReset) {
  auto unowned_delegate1 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm1(alarm_factory_.CreateAlarm(unowned_delegate1));
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(10));
  auto unowned_delegate2 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm2(alarm_factory_.CreateAlarm(unowned_delegate2));
  alarm2->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(10));
  EXPECT_TRUE(alarm2->IsSet());

  // Reset alarm1 to a different deadline.
  alarm1->Cancel();
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(5));
  // Advance 9us, alarm1 should have fired but alarm2 shouldn't.
  advanceUsAndLoop(9);
  EXPECT_TRUE(unowned_delegate1->fired());
  EXPECT_FALSE(unowned_delegate2->fired());
}

TEST_F(EnvoyQuicAlarmTest, CreateAlarmAndUpdate) {
  auto unowned_delegate1 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm1(alarm_factory_.CreateAlarm(unowned_delegate1));
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(10));
  auto unowned_delegate2 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm2(alarm_factory_.CreateAlarm(unowned_delegate2));
  alarm2->Set(clock_.Now() + QuicTime::Delta::FromMicroseconds(10));
  EXPECT_TRUE(alarm2->IsSet());

  // Update alarm1 to an earlier deadline.
  alarm1->Update(clock_.Now() + QuicTime::Delta::FromMicroseconds(5),
                 quic::QuicTime::Delta::Zero());
  // Advance 9us, alarm1 should have fired but alarm2 shouldn't.
  advanceUsAndLoop(9);
  EXPECT_TRUE(unowned_delegate1->fired());
  EXPECT_FALSE(unowned_delegate2->fired());
}

TEST_F(EnvoyQuicAlarmTest, SetAlarmToPastTime) {
  advanceUsAndLoop(100);
  EXPECT_EQ(100, (clock_.Now() - quic::QuicTime::Zero()).ToMicroseconds());
  auto unowned_delegate = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm(alarm_factory_.CreateAlarm(unowned_delegate));
  alarm->Set(clock_.Now() - QuicTime::Delta::FromMicroseconds(10));
  base_scheduler_.run(Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(unowned_delegate->fired());
}

TEST_F(EnvoyQuicAlarmTest, CancelActiveAlarm) {
  advanceUsAndLoop(100);
  EXPECT_EQ(100, (clock_.Now() - quic::QuicTime::Zero()).ToMicroseconds());
  auto unowned_delegate = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm(alarm_factory_.CreateAlarm(unowned_delegate));
  // alarm becomes active upon Set().
  alarm->Set(clock_.Now() - QuicTime::Delta::FromMicroseconds(10));
  alarm->Cancel();
  base_scheduler_.run(Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(unowned_delegate->fired());
}

} // namespace Quic
} // namespace Envoy
