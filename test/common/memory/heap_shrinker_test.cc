#include "common/event/dispatcher_impl.h"
#include "common/memory/heap_shrinker.h"
#include "common/memory/stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Memory {
namespace {

class HeapShrinkerTest : public testing::Test {
protected:
  HeapShrinkerTest()
      : api_(Api::createApiForTest(stats_, time_system_)),
        dispatcher_("test_thread", *api_, time_system_) {}

  void step() {
    time_system_.advanceTimeAndRun(std::chrono::milliseconds(10000), dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);
  }

  Envoy::Stats::TestUtil::TestStore stats_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherImpl dispatcher_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
  Event::TimerCb timer_cb_;
};

TEST_F(HeapShrinkerTest, DoNotShrinkWhenNotConfigured) {
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(overload_manager_, registerForAction(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(dispatcher, createTimer_(_)).Times(0);
  HeapShrinker h(dispatcher, overload_manager_, stats_);
}

TEST_F(HeapShrinkerTest, ShrinkWhenTriggered) {
  Server::OverloadActionCb action_cb;
  EXPECT_CALL(overload_manager_, registerForAction(_, _, _))
      .WillOnce(Invoke([&](const std::string&, Event::Dispatcher&, Server::OverloadActionCb cb) {
        action_cb = cb;
        return true;
      }));

  HeapShrinker h(dispatcher_, overload_manager_, stats_);

  auto data = std::make_unique<char[]>(5000000);
  const uint64_t physical_mem_before_shrink =
      Stats::totalCurrentlyReserved() - Stats::totalPageHeapUnmapped();
  data.reset();

  Envoy::Stats::Counter& shrink_count =
      stats_.counter("overload.envoy.overload_actions.shrink_heap.shrink_count");
  action_cb(Server::OverloadActionState::saturated());
  step();
  EXPECT_EQ(1, shrink_count.value());

  const uint64_t physical_mem_after_shrink =
      Stats::totalCurrentlyReserved() - Stats::totalPageHeapUnmapped();
#if defined(TCMALLOC) || defined(GPERFTOOLS_TCMALLOC)
  EXPECT_GE(physical_mem_before_shrink, physical_mem_after_shrink);
#else
  EXPECT_EQ(physical_mem_before_shrink, physical_mem_after_shrink);
#endif
  Stats::dumpStatsToLog();

  step();
  EXPECT_EQ(2, shrink_count.value());

  action_cb(Server::OverloadActionState::inactive());
  step();
  step();
  EXPECT_EQ(2, shrink_count.value());
}

} // namespace
} // namespace Memory
} // namespace Envoy
