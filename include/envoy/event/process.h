#pragma once

#include <functional>

namespace Envoy {
namespace Event {

/**
 * Callback invoked when a child is terminated.
 * @param zero_exit_code True if the process exited with a zero exit code, false
 *    if the process exited with non-zero status or was killed because of a signal.
 */
typedef std::function<void(bool zero_exit_code)> ProcessTerminationCb;

/**
 * An abstract child process. Free the object to kill the process before completion.
 * Freeing the object after the process termination callback has run has no effect.
 */
class ChildProcess {
public:
  virtual ~ChildProcess() {}
};

typedef std::unique_ptr<ChildProcess> ChildProcessPtr;

} // namespace Event
} // namespace Envoy
