#include "source/common/profiler/profiler.h"

#include <string>

#include "source/common/common/thread.h"

#ifdef PROFILER_AVAILABLE

#include "gperftools/heap-profiler.h"
#include "gperftools/profiler.h"

namespace Envoy {
namespace Profiler {

bool Cpu::profilerEnabled() { return ProfilingIsEnabledForAllThreads(); }

bool Cpu::startProfiler(const std::string& output_path) {
  return ProfilerStart(output_path.c_str());
}

void Cpu::stopProfiler() { ProfilerStop(); }

bool Heap::profilerEnabled() {
  // determined by PROFILER_AVAILABLE
  return true;
}

bool Heap::isProfilerStarted() { return IsHeapProfilerRunning(); }
bool Heap::startProfiler(const std::string& output_file_name_prefix) {
  HeapProfilerStart(output_file_name_prefix.c_str());
  return true;
}

bool Heap::stopProfiler() {
  if (!IsHeapProfilerRunning()) {
    return false;
  }
  HeapProfilerDump("stop and dump");
  HeapProfilerStop();
  return true;
}

} // namespace Profiler
} // namespace Envoy

#else

namespace Envoy {
namespace Profiler {

bool Cpu::profilerEnabled() { return false; }
bool Cpu::startProfiler(const std::string&) { return false; }
void Cpu::stopProfiler() {}

bool Heap::profilerEnabled() { return false; }
bool Heap::isProfilerStarted() { return false; }
bool Heap::startProfiler(const std::string&) { return false; }
bool Heap::stopProfiler() { return false; }

} // namespace Profiler
} // namespace Envoy

#endif // #ifdef PROFILER_AVAILABLE

#ifdef TCMALLOC

#include "tcmalloc/malloc_extension.h"
#include "tcmalloc/profile_marshaler.h"

namespace Envoy {
namespace Profiler {

static tcmalloc::MallocExtension::AllocationProfilingToken* alloc_profiler = nullptr;

absl::StatusOr<std::string> TcmallocProfiler::tcmallocHeapProfile() {
  auto profile = tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kHeap);
  return tcmalloc::Marshal(profile);
}

absl::Status TcmallocProfiler::startAllocationProfile() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (alloc_profiler != nullptr) {
    return absl::Status(absl::StatusCode::kFailedPrecondition,
                        "Allocation profiler has already started");
  }
  alloc_profiler = new tcmalloc::MallocExtension::AllocationProfilingToken(
      tcmalloc::MallocExtension::StartAllocationProfiling());
  return absl::OkStatus();
}

absl::StatusOr<std::string> TcmallocProfiler::stopAllocationProfile() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (!alloc_profiler) {
    return absl::Status(absl::StatusCode::kFailedPrecondition,
                        "Allocation profiler is not started");
  }
  const auto profile = std::move(*alloc_profiler).Stop();
  const auto result = tcmalloc::Marshal(profile);
  delete alloc_profiler;
  alloc_profiler = nullptr;
  return result;
}

} // namespace Profiler
} // namespace Envoy

#else

namespace Envoy {
namespace Profiler {

absl::StatusOr<std::string> TcmallocProfiler::tcmallocHeapProfile() {
  return absl::Status(absl::StatusCode::kUnimplemented,
                      "Heap profile is not implemented in current build");
}

absl::Status TcmallocProfiler::startAllocationProfile() {
  return absl::Status(absl::StatusCode::kUnimplemented,
                      "Allocation profile is not implemented in current build");
}

absl::StatusOr<std::string> TcmallocProfiler::stopAllocationProfile() {
  return absl::Status(absl::StatusCode::kUnimplemented,
                      "Allocation profile is not implemented in current build");
}

} // namespace Profiler
} // namespace Envoy

#endif // #ifdef TCMALLOC
