#include "server/hot_restart_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Server {

TEST(HotRestartImplTest, alloc) {
  Api::MockOsSysCalls os_sys_calls;
  NiceMock<MockOptions> options;
  std::vector<uint8_t> buffer;

  EXPECT_CALL(os_sys_calls, shmUnlink(_));
  EXPECT_CALL(os_sys_calls, shmOpen(_, _, _));
  EXPECT_CALL(os_sys_calls, ftruncate(_, _)).WillOnce(WithArg<1>(Invoke([&buffer](off_t size) {
    buffer.resize(size);
    return 0;
  })));
  EXPECT_CALL(os_sys_calls, mmap(_, _, _, _, _, _)).WillOnce(InvokeWithoutArgs([&buffer]() {
    return buffer.data();
  }));
  EXPECT_CALL(os_sys_calls, bind(_, _, _));

  // Test we match the correct stat with empty-slots before, after, or both.
  HotRestartImpl hot_restart1(options, os_sys_calls);
  hot_restart1.drainParentListeners();
  Stats::RawStatData* stat1 = hot_restart1.alloc("stat1");
  Stats::RawStatData* stat2 = hot_restart1.alloc("stat2");
  Stats::RawStatData* stat3 = hot_restart1.alloc("stat3");
  Stats::RawStatData* stat4 = hot_restart1.alloc("stat4");
  Stats::RawStatData* stat5 = hot_restart1.alloc("stat5");
  hot_restart1.free(*stat2);
  hot_restart1.free(*stat4);
  stat2 = nullptr;
  stat4 = nullptr;

  EXPECT_CALL(options, restartEpoch()).WillRepeatedly(Return(1));
  EXPECT_CALL(os_sys_calls, shmOpen(_, _, _));
  EXPECT_CALL(os_sys_calls, mmap(_, _, _, _, _, _)).WillOnce(Return(buffer.data()));
  EXPECT_CALL(os_sys_calls, bind(_, _, _));
  HotRestartImpl hot_restart2(options, os_sys_calls);
  Stats::RawStatData* stat1_prime = hot_restart2.alloc("stat1");
  Stats::RawStatData* stat3_prime = hot_restart2.alloc("stat3");
  Stats::RawStatData* stat5_prime = hot_restart2.alloc("stat5");
  EXPECT_EQ(stat1, stat1_prime);
  EXPECT_EQ(stat3, stat3_prime);
  EXPECT_EQ(stat5, stat5_prime);
}

} // namespace Server
} // namespace Envoy
