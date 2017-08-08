#pragma once

#include <string>

namespace Envoy {
namespace Profiler {

/**
 * Process wide CPU profiling.
 */
class Cpu {
public:
  /**
   * @return whether the profiler is enabled or not.
   */
  static bool profilerEnabled();

  /**
   * Start the profiler and write to the specified path.
   * @return bool whether the call to start the profiler succeeded.
   */
  static bool startProfiler(const std::string& output_path);

  /**
   * Stop the profiler.
   */
  static void stopProfiler();
};

/**
 * Process wide heap profiling
 */
class Heap {
private:
  static void forceLink();
};

} // namespace Profiler
} // namespace Envoy
