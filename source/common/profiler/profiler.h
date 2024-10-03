#pragma once

#include <string>

#include "absl/status/statusor.h"

// Profiling support is provided in the release tcmalloc of `gperftools`, but not in the library
// that supplies the debug tcmalloc. So all the profiling code must be ifdef'd
// on PROFILER_AVAILABLE which is dependent on those two settings.
#if defined(GPERFTOOLS_TCMALLOC) && !defined(ENVOY_MEMORY_DEBUG_ENABLED)
#define PROFILER_AVAILABLE
#endif

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
public:
  /**
   * @return whether the profiler is enabled in this build or not.
   */
  static bool profilerEnabled();

  /**
   * @return whether the profiler is started or not
   */
  static bool isProfilerStarted();

  /**
   * Start the profiler and write to the specified path.
   * @return bool whether the call to start the profiler succeeded.
   */
  static bool startProfiler(const std::string& output_path);

  /**
   * Stop the profiler.
   * @return bool whether the file is dumped
   */
  static bool stopProfiler();
};

/**
 * Default profiler which will be enabled when tcmalloc (not `gperftools`) is used.
 * This class is not thread-safe.
 */
class TcmallocProfiler {
public:
  TcmallocProfiler() = default;

  static absl::StatusOr<std::string> tcmallocHeapProfile();
  static absl::Status startAllocationProfile();
  static absl::StatusOr<std::string> stopAllocationProfile();
};

} // namespace Profiler
} // namespace Envoy
