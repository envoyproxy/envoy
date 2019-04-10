#include "common/common/thread_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {

namespace Thread {

// TODO(sesmith177) Tests should get the ThreadFactory from the same location as the main code
ThreadFactory& threadFactoryForTest() {
#ifdef WIN32
  static ThreadFactoryImplWin32* thread_factory = new ThreadFactoryImplWin32();
#else
  static ThreadFactoryImplPosix* thread_factory = new ThreadFactoryImplPosix();
#endif
  return *thread_factory;
}

} // namespace Thread

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
