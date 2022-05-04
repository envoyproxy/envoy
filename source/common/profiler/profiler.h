#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/runtime/runtime_impl.h"

#include <chrono>
#include <cstdint>
#include <string>

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

#ifdef TCMALLOC

/**
 * Default heap profiler which will be enabled when tcmalloc (not `gperftools`) is used.
 */
class TcmallocHeapProfiler {
public:
  TcmallocHeapProfiler() = default;

  bool heapProfilerStarted() const { return heap_profile_started_; }

  void startProfiler(absl::string_view path, std::chrono::milliseconds period);
  void stopProfiler();

private:
  bool heap_profile_started_{};
  uint64_t next_file_id_{};
  std::string heap_file_path_;
  std::chrono::milliseconds period_;
  Event::TimerPtr flush_timer_;
};

#endif

} // namespace Profiler
} // namespace Envoy
