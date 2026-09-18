#pragma once

#include <optional>
#include <string>

#include "envoy/filesystem/filesystem.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

// Files under /proc describing the calling process' cgroup membership and its mounts.
constexpr absl::string_view DefaultProcSelfCgroup = "/proc/self/cgroup";
constexpr absl::string_view DefaultProcSelfMountInfo = "/proc/self/mountinfo";

/**
 * Resolve the cgroup v2 directory of the calling container.
 *
 * The cgroup2 mount point is not necessarily the container's own cgroup. With a private cgroup
 * namespace it is, but in the host cgroup namespace it is the cgroup root, where per-cgroup
 * interface files such as cpu.max do not exist. Joining the mount point with the path reported by
 * /proc/self/cgroup yields the container's own directory in both cases.
 *
 * The returned directory is not validated; callers are expected to fall back to the mount point
 * when it does not expose the files they need.
 *
 * @param fs Filesystem instance to use for file operations.
 * @param proc_self_cgroup path to the cgroup membership file.
 * @param proc_self_mountinfo path to the mount table file.
 * @return the resolved directory, or nullopt if it cannot be determined.
 */
std::optional<std::string>
resolveCgroupV2Dir(Filesystem::Instance& fs,
                   absl::string_view proc_self_cgroup = DefaultProcSelfCgroup,
                   absl::string_view proc_self_mountinfo = DefaultProcSelfMountInfo);

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
