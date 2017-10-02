#include "server/hot_restart_impl.h"

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
  MockOsSysCalls os_sys_calls;
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

  HotRestartImpl hot_restart1(options, os_sys_calls);
  hot_restart1.drainParentListeners();
  Stats::RawStatData* stat1 = hot_restart1.alloc("stat1");
  Stats::RawStatData* stat2 = hot_restart1.alloc("stat2");
  hot_restart1.free(*stat1);
  stat1 = nullptr;

  EXPECT_CALL(options, restartEpoch()).WillRepeatedly(Return(1));
  EXPECT_CALL(os_sys_calls, shmOpen(_, _, _));
  EXPECT_CALL(os_sys_calls, mmap(_, _, _, _, _, _)).WillOnce(Return(buffer.data()));
  HotRestartImpl hot_restart2(options, os_sys_calls);
  Stats::RawStatData* stat3 = hot_restart2.alloc("stat2");
  EXPECT_EQ(stat2, stat3);
}

} // namespace Server
} // namespace Envoy
