#include <cstdint>
#include <vector>

#include "source/common/common/cpu_affinity.h"

#include "gtest/gtest.h"

#if defined(__linux__)
#include <sched.h>

#include <cerrno>

#include "source/common/api/os_sys_calls_impl_linux.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#endif

namespace Envoy {
namespace Thread {
namespace {

#if defined(__linux__)
using testing::_;
using testing::NiceMock;
using testing::Return;
#endif

// Zero workers never produce an assignment on any platform.
TEST(CpuAffinityTest, NoAssignmentForZeroWorkers) { EXPECT_TRUE(workerCpuAssignment(0).empty()); }

#if defined(__linux__)
TEST(CpuAffinityTest, WorkerAssignmentTakesLeadingCpus) {
  // No assignment when more workers are requested than available CPUs.
  EXPECT_TRUE(workerCpuAssignment(CPU_SETSIZE + 1).empty());

  // The affinity set is non empty on a normal host, and an assignment takes the leading CPUs of
  // that set in order, mapping worker i to entry i.
  const std::vector<uint32_t> cpus = cpuAffinitySet();
  ASSERT_FALSE(cpus.empty());
  const std::vector<uint32_t> one = workerCpuAssignment(1);
  ASSERT_EQ(1, one.size());
  EXPECT_EQ(cpus[0], one[0]);
  if (cpus.size() >= 2) {
    const std::vector<uint32_t> two = workerCpuAssignment(2);
    ASSERT_EQ(2, two.size());
    EXPECT_EQ(cpus[0], two[0]);
    EXPECT_EQ(cpus[1], two[1]);
  }
}

TEST(CpuAffinityTest, AffinitySetEmptyOnQueryFailure) {
  NiceMock<Api::MockLinuxOsSysCalls> linux_os_sys_calls;
  TestThreadsafeSingletonInjector<Api::LinuxOsSysCallsImpl> injector{&linux_os_sys_calls};
  EXPECT_CALL(linux_os_sys_calls, sched_getaffinity(_, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, EINVAL}));
  EXPECT_TRUE(cpuAffinitySet().empty());
}
#endif

} // namespace
} // namespace Thread
} // namespace Envoy
