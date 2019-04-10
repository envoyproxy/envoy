#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

namespace Envoy {

namespace Thread {
ThreadFactory& threadFactoryForTest();
} // namespace Thread

namespace Filesystem {
Instance& fileSystemForTest();
} // namespace Filesystem

} // namespace Envoy
