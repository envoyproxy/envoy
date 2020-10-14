#pragma once

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {
/**
 * Tries to terminates the process by killing the thread specified by
 * the ThreadId. The implementation is platform dependent and currently
 * only works on platforms that support SIGABRT.
 *
 * Returns the result from the platform specific function (i.e. kill) to terminate
 * the thread. If the platform is currently unsupported, this will return false.
 */
bool terminateThread(const ThreadId& tid);

} // namespace Thread
} // namespace Envoy
