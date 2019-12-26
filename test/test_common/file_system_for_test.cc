#include "common/filesystem/filesystem_impl.h"

namespace Envoy {

namespace Filesystem {

// TODO(sesmith177) Tests should get the Filesystem::Instance from the same location as the main
// code
Instance& fileSystemForTest() {
#ifdef WIN32
  static InstanceImplWin32* file_system = new InstanceImplWin32();
#else
  static InstanceImplPosix* file_system = new InstanceImplPosix();
#endif
  return *file_system;
}

} // namespace Filesystem

} // namespace Envoy
