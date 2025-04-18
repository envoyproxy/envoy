#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_paths.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

namespace {

class RealFileSystem : public FileSystem {
public:
  bool exists(const std::string& path) const override { return std::filesystem::exists(path); }
};

} // namespace

// Initialize with real implementation
const FileSystem* FileSystem::instance_ = new RealFileSystem();

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
