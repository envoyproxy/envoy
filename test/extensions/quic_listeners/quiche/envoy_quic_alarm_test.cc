#include "extensions/quic_listeners/quiche/envoy_quic_alarm.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Event::Dispatcher;
using quic::QuicTime;

namespace Envoy {
namespace Quic {

class TestDelegate : public quic::QuicAlarm::Delegate {
public:
  TestDelegate() = default;

  // quic::QuicAlarm::Delegate
  void OnAlarm() override { fired_ = true; }

  bool fired() const { return fired_; }
  void set_fired(bool fired) { fired_ = fired; }

private:
  bool fired_{false};
};

class EnvoyQuicAlarmTest : public ::testing::Test {
public:
  EnvoyQuicAlarmTest()
      : api_(Api::createApiForTest(time_system_)), dispatcher_(api_->allocateDispatcher()),
        clock_(*dispatcher_), alarm_factory_(*dispatcher_, clock_) {}

  void advanceMsAndLoop(int64_t delay_ms) {
    time_system_.sleep(std::chrono::milliseconds(delay_ms));
    dispatcher_->run(Dispatcher::RunType::NonBlock);
  }

protected:
  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EnvoyQuicClock clock_;
  EnvoyQuicAlarmFactory alarm_factory_;
  quic::QuicConnectionArena arena_;
};

TEST_F(EnvoyQuicAlarmTest, CreateAlarmByFactory) {
  auto unowned_delegate = new TestDelegate();
  quic::QuicAlarm* alarm = alarm_factory_.CreateAlarm(unowned_delegate);
  alarm->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  // Advance 9us, alarm shouldn't fire.
  advanceMsAndLoop(9);
  EXPECT_FALSE(unowned_delegate->fired());
  // Advance 1us, alarm should have fired.
  advanceMsAndLoop(1);
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
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  EXPECT_TRUE(alarm1->IsSet());
  auto unowned_delegate2 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm2(alarm_factory_.CreateAlarm(unowned_delegate2));
  alarm2->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  EXPECT_TRUE(alarm2->IsSet());

  alarm1->Cancel();
  EXPECT_FALSE(alarm1->IsSet());
  // Advance 10us, alarm1 shouldn't fire, but alarm2 should.
  advanceMsAndLoop(10);
  EXPECT_TRUE(unowned_delegate2->fired());
  EXPECT_FALSE(unowned_delegate1->fired());
}

TEST_F(EnvoyQuicAlarmTest, CreateAlarmAndReset) {
  auto unowned_delegate1 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm1(alarm_factory_.CreateAlarm(unowned_delegate1));
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  auto unowned_delegate2 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm2(alarm_factory_.CreateAlarm(unowned_delegate2));
  alarm2->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  EXPECT_TRUE(alarm2->IsSet());

  // Reset alarm1 to a different deadline.
  alarm1->Cancel();
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(5));
  // Advance 9us, alarm1 should have fired but alarm2 shouldn't.
  advanceMsAndLoop(9);
  EXPECT_TRUE(unowned_delegate1->fired());
  EXPECT_FALSE(unowned_delegate2->fired());

  advanceMsAndLoop(1);
  EXPECT_TRUE(unowned_delegate2->fired());
}

TEST_F(EnvoyQuicAlarmTest, CreateAlarmAndUpdate) {
  auto unowned_delegate1 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm1(alarm_factory_.CreateAlarm(unowned_delegate1));
  alarm1->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  auto unowned_delegate2 = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm2(alarm_factory_.CreateAlarm(unowned_delegate2));
  alarm2->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  EXPECT_TRUE(alarm2->IsSet());

  // Update alarm1 to an earlier deadline.
  alarm1->Update(clock_.Now() + QuicTime::Delta::FromMilliseconds(5),
                 quic::QuicTime::Delta::Zero());
  // Advance 9us, alarm1 should have fired but alarm2 shouldn't.
  advanceMsAndLoop(9);
  EXPECT_TRUE(unowned_delegate1->fired());
  EXPECT_FALSE(unowned_delegate2->fired());

  advanceMsAndLoop(1);
  EXPECT_TRUE(unowned_delegate2->fired());
}

TEST_F(EnvoyQuicAlarmTest, PostponeDeadline) {
  auto unowned_delegate = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm(alarm_factory_.CreateAlarm(unowned_delegate));
  alarm->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  advanceMsAndLoop(9);
  EXPECT_FALSE(unowned_delegate->fired());
  // Postpone deadline to a later time.
  alarm->Update(clock_.Now() + QuicTime::Delta::FromMilliseconds(5), quic::QuicTime::Delta::Zero());
  advanceMsAndLoop(1);
  EXPECT_EQ(10, (clock_.Now() - quic::QuicTime::Zero()).ToMilliseconds());
  // alarm shouldn't fire at old deadline.
  EXPECT_FALSE(unowned_delegate->fired());

  advanceMsAndLoop(4);
  // alarm should fire at new deadline.
  EXPECT_TRUE(unowned_delegate->fired());
}

TEST_F(EnvoyQuicAlarmTest, SetAlarmToPastTime) {
  advanceMsAndLoop(100);
  EXPECT_EQ(100, (clock_.Now() - quic::QuicTime::Zero()).ToMilliseconds());
  auto unowned_delegate = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm(alarm_factory_.CreateAlarm(unowned_delegate));
  // alarm becomes active upon Set().
  alarm->Set(clock_.Now() - QuicTime::Delta::FromMilliseconds(10));
  EXPECT_FALSE(unowned_delegate->fired());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(unowned_delegate->fired());
}

TEST_F(EnvoyQuicAlarmTest, UpdateAlarmWithPastDeadline) {
  auto unowned_delegate = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm(alarm_factory_.CreateAlarm(unowned_delegate));
  alarm->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  advanceMsAndLoop(9);
  EXPECT_EQ(9, (clock_.Now() - quic::QuicTime::Zero()).ToMilliseconds());
  EXPECT_FALSE(unowned_delegate->fired());
  // alarm becomes active upon Update().
  alarm->Update(clock_.Now() - QuicTime::Delta::FromMilliseconds(1), quic::QuicTime::Delta::Zero());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(unowned_delegate->fired());
  unowned_delegate->set_fired(false);
  advanceMsAndLoop(1);
  // alarm shouldn't fire at the original deadline.
  EXPECT_FALSE(unowned_delegate->fired());
}

TEST_F(EnvoyQuicAlarmTest, CancelActiveAlarm) {
  advanceMsAndLoop(100);
  EXPECT_EQ(100, (clock_.Now() - quic::QuicTime::Zero()).ToMilliseconds());
  auto unowned_delegate = new TestDelegate();
  quic::QuicArenaScopedPtr<quic::QuicAlarm> alarm(alarm_factory_.CreateAlarm(unowned_delegate));
  // alarm becomes active upon Set().
  alarm->Set(clock_.Now() - QuicTime::Delta::FromMilliseconds(10));
  alarm->Cancel();
  dispatcher_->run(Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(unowned_delegate->fired());
}

TEST_F(EnvoyQuicAlarmTest, CancelUponDestruction) {
  auto unowned_delegate = new TestDelegate();
  quic::QuicAlarm* alarm = alarm_factory_.CreateAlarm(unowned_delegate);
  // alarm becomes active upon Set().
  alarm->Set(clock_.Now() + QuicTime::Delta::FromMilliseconds(10));
  // delegate should be destroyed with alarm.
  delete alarm;
  // alarm firing callback should have been cancelled, otherwise the delegate
  // would be used after free.
  advanceMsAndLoop(10);
}

} // namespace Quic
} // namespace Envoy
