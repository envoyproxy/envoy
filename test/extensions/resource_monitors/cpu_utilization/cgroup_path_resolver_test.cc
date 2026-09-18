#include "source/extensions/resource_monitors/cpu_utilization/cgroup_path_resolver.h"

#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {
namespace {

using testing::Return;

constexpr absl::string_view ContainerCgroup =
    "/kubepods.slice/kubepods-pod0e4c6b7a_1111_2222_3333_44445555aaaa.slice/"
    "cri-containerd-8e257263d11a92fceed0d99cd1812148ec0971d72ff1e9ac677cf10f21636afe.scope";

// A cgroup2 mount line as seen in the host cgroup namespace: the mount exposes the whole
// hierarchy ("/") at /sys/fs/cgroup.
std::string hostMountInfo() {
  return "2847 2846 0:29 / /proc rw,nosuid - proc proc rw\n"
         "2848 2847 0:30 / /sys/fs/cgroup rw,nosuid,nodev,noexec,relatime - cgroup2 cgroup rw\n";
}

void expectReads(Filesystem::MockInstance& fs, const std::string& mountinfo,
                 const std::string& cgroup) {
  EXPECT_CALL(fs, fileReadToEnd("/proc/self/mountinfo")).WillOnce(Return(mountinfo));
  EXPECT_CALL(fs, fileReadToEnd("/proc/self/cgroup")).WillOnce(Return(cgroup));
}

// In the host cgroup namespace the mount point is the cgroup root, so the container's cgroup path
// has to be appended to reach its own directory.
TEST(CgroupPathResolverTest, HostCgroupNamespaceJoinsMountPointAndCgroupPath) {
  Filesystem::MockInstance fs;
  expectReads(fs, hostMountInfo(), absl::StrCat("0::", ContainerCgroup, "\n"));

  const auto dir = resolveCgroupV2Dir(fs);
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(*dir, absl::StrCat("/sys/fs/cgroup", ContainerCgroup));
}

// A private cgroup namespace reports "0::/" because the mount point already is the container's
// own cgroup, so nothing is appended.
TEST(CgroupPathResolverTest, PrivateCgroupNamespaceReturnsMountPoint) {
  Filesystem::MockInstance fs;
  expectReads(fs, hostMountInfo(), "0::/\n");

  const auto dir = resolveCgroupV2Dir(fs);
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(*dir, "/sys/fs/cgroup");
}

// When the mount exposes a subtree rather than the root, only the part of the cgroup path below
// that subtree is appended.
TEST(CgroupPathResolverTest, SubtreeMountStripsMountRoot) {
  Filesystem::MockInstance fs;
  expectReads(fs, "100 99 0:30 /kubepods.slice /sys/fs/cgroup rw,relatime - cgroup2 cgroup rw\n",
              "0::/kubepods.slice/pod.slice/container.scope\n");

  const auto dir = resolveCgroupV2Dir(fs);
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(*dir, "/sys/fs/cgroup/pod.slice/container.scope");
}

// A cgroup that is not under the mounted subtree cannot be reached through this mount, so the
// mount point is returned unchanged.
TEST(CgroupPathResolverTest, CgroupOutsideMountRootReturnsMountPoint) {
  Filesystem::MockInstance fs;
  expectReads(fs, "100 99 0:30 /kubepods.slice /sys/fs/cgroup rw - cgroup2 cgroup rw\n",
              "0::/system.slice/some.service\n");

  const auto dir = resolveCgroupV2Dir(fs);
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(*dir, "/sys/fs/cgroup");
}

TEST(CgroupPathResolverTest, UnreadableMountInfoReturnsNullopt) {
  Filesystem::MockInstance fs;
  EXPECT_CALL(fs, fileReadToEnd("/proc/self/mountinfo"))
      .WillOnce(Return(absl::NotFoundError("nope")));

  EXPECT_FALSE(resolveCgroupV2Dir(fs).has_value());
}

// A cgroup v1 host has no cgroup2 mount, so there is nothing to resolve.
TEST(CgroupPathResolverTest, NoCgroupV2MountReturnsNullopt) {
  Filesystem::MockInstance fs;
  EXPECT_CALL(fs, fileReadToEnd("/proc/self/mountinfo"))
      .WillOnce(Return("30 29 0:26 / /sys/fs/cgroup/cpu rw - cgroup cgroup rw,cpu\n"));

  EXPECT_FALSE(resolveCgroupV2Dir(fs).has_value());
}

TEST(CgroupPathResolverTest, UnreadableCgroupFileReturnsNullopt) {
  Filesystem::MockInstance fs;
  EXPECT_CALL(fs, fileReadToEnd("/proc/self/mountinfo")).WillOnce(Return(hostMountInfo()));
  EXPECT_CALL(fs, fileReadToEnd("/proc/self/cgroup")).WillOnce(Return(absl::NotFoundError("nope")));

  EXPECT_FALSE(resolveCgroupV2Dir(fs).has_value());
}

// Only the unified hierarchy ("0::") describes cgroup v2 membership.
TEST(CgroupPathResolverTest, NoUnifiedHierarchyLineReturnsNullopt) {
  Filesystem::MockInstance fs;
  expectReads(fs, hostMountInfo(), "4:cpu,cpuacct:/kubepods/pod/container\n2:memory:/kubepods\n");

  EXPECT_FALSE(resolveCgroupV2Dir(fs).has_value());
}

// Lines without the " - " separator or with too few positional fields are not usable.
TEST(CgroupPathResolverTest, MalformedMountInfoLinesAreSkipped) {
  Filesystem::MockInstance fs;
  EXPECT_CALL(fs, fileReadToEnd("/proc/self/mountinfo"))
      .WillOnce(Return("this line has no separator at all\n"
                       "1 2 0:30 - cgroup2 cgroup rw\n"));

  EXPECT_FALSE(resolveCgroupV2Dir(fs).has_value());
}

TEST(CgroupPathResolverTest, EmptyUnifiedPathReturnsMountPoint) {
  Filesystem::MockInstance fs;
  expectReads(fs, hostMountInfo(), "0::\n");

  const auto dir = resolveCgroupV2Dir(fs);
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(*dir, "/sys/fs/cgroup");
}

TEST(CgroupPathResolverTest, CustomProcPathsAreUsed) {
  Filesystem::MockInstance fs;
  EXPECT_CALL(fs, fileReadToEnd("/fake/mountinfo")).WillOnce(Return(hostMountInfo()));
  EXPECT_CALL(fs, fileReadToEnd("/fake/cgroup")).WillOnce(Return("0::/a/b\n"));

  const auto dir = resolveCgroupV2Dir(fs, "/fake/cgroup", "/fake/mountinfo");
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(*dir, "/sys/fs/cgroup/a/b");
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
