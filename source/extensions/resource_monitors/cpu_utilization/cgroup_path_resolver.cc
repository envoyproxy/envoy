#include "source/extensions/resource_monitors/cpu_utilization/cgroup_path_resolver.h"

#include <optional>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {

namespace {

constexpr absl::string_view CgroupV2FsType = "cgroup2";
constexpr absl::string_view MountInfoSeparator = " - ";
constexpr absl::string_view UnifiedHierarchyPrefix = "0::";

// The portion of the hierarchy a cgroup2 mount exposes, and where it is mounted.
struct CgroupV2Mount {
  std::string root;
  std::string mount_point;
};

// Only the fields before the " - " separator in a mountinfo line are positional, which puts the
// mount root at index 3 and the mount point at index 4. The filesystem type is the first field
// after that separator.
std::optional<CgroupV2Mount> findCgroupV2Mount(absl::string_view mountinfo) {
  for (const absl::string_view line : absl::StrSplit(mountinfo, '\n', absl::SkipEmpty())) {
    const size_t separator = line.find(MountInfoSeparator);
    if (separator == absl::string_view::npos) {
      continue;
    }
    const std::vector<absl::string_view> post =
        absl::StrSplit(line.substr(separator + MountInfoSeparator.size()), ' ', absl::SkipEmpty());
    if (post.empty() || post[0] != CgroupV2FsType) {
      continue;
    }
    const std::vector<absl::string_view> pre =
        absl::StrSplit(line.substr(0, separator), ' ', absl::SkipEmpty());
    if (pre.size() < 5) {
      continue;
    }
    return CgroupV2Mount{std::string(pre[3]), std::string(pre[4])};
  }
  return std::nullopt;
}

// The unified hierarchy is reported as a single "0::<path>" line.
std::optional<std::string> findUnifiedCgroupPath(absl::string_view contents) {
  for (const absl::string_view line : absl::StrSplit(contents, '\n', absl::SkipEmpty())) {
    if (absl::StartsWith(line, UnifiedHierarchyPrefix)) {
      return std::string(
          absl::StripTrailingAsciiWhitespace(line.substr(UnifiedHierarchyPrefix.size())));
    }
  }
  return std::nullopt;
}

} // namespace

std::optional<std::string> resolveCgroupV2Dir(Filesystem::Instance& fs,
                                              absl::string_view proc_self_cgroup,
                                              absl::string_view proc_self_mountinfo) {
  const auto mountinfo = fs.fileReadToEnd(std::string(proc_self_mountinfo));
  if (!mountinfo.ok()) {
    return std::nullopt;
  }
  const auto mount = findCgroupV2Mount(mountinfo.value());
  if (!mount.has_value()) {
    return std::nullopt;
  }

  const auto cgroup = fs.fileReadToEnd(std::string(proc_self_cgroup));
  if (!cgroup.ok()) {
    return std::nullopt;
  }
  const auto cgroup_path = findUnifiedCgroupPath(cgroup.value());
  if (!cgroup_path.has_value()) {
    return std::nullopt;
  }

  // Translate the cgroup path into the mounted view by stripping the part of the hierarchy that
  // the mount does not expose. Anything left over is appended to the mount point. A cgroup outside
  // the mounted subtree leaves nothing to append, which is also what a private cgroup namespace
  // produces: it reports "/" because the mount point already is the container's own cgroup.
  absl::string_view relative;
  if (mount->root == "/") {
    relative = *cgroup_path;
  } else if (absl::StartsWith(*cgroup_path, mount->root)) {
    relative = absl::string_view(*cgroup_path).substr(mount->root.size());
  }

  if (relative.empty() || relative == "/") {
    return mount->mount_point;
  }
  return absl::StrCat(mount->mount_point, relative);
}

} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
