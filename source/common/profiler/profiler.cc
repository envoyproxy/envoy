#include "source/common/profiler/profiler.h"

#include <chrono>
#include <fstream>
#include <string>

#include "source/common/common/logger.h"

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

#elif defined TCMALLOC

#include "tcmalloc/malloc_extension.h"
#include "tcmalloc/profile_marshaler.h"

namespace Envoy {
namespace Profiler {

TcmallocHeapProfiler& tcmallocHeapProfiler() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(TcmallocHeapProfiler);
}

bool TcmallocHeapProfiler::startProfiler(absl::string_view path, std::chrono::milliseconds period) {
  if (heap_profile_started_ || path.empty() || period.count() == 0) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::main), warn,
                        "Profile: tcmalloc heap profiler was started or profile file path is empty "
                        "or output period is zero");
    return false;
  }

  heap_file_path_ = std::string(path);
  period_ = period;

  ASSERT(period_.count() > 0);

  // TODO(wbpcode): should we create new independent thread for this?
  flush_timer_ = Event::MainDispatcherSingleton::get().createTimer([this]() {
    auto profile = tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kHeap);
    absl::StatusOr<std::string> result = tcmalloc::Marshal(profile);

    if (!result.ok()) {
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::main), warn,
                          "Profile: get nothing from tcmalloc profile");
      return;
    }
    const std::string output_file_path = heap_file_path_ + std::to_string(next_file_id_++);
    std::ofstream output_file(output_file_path, std::ios_base::binary);

    if (!output_file.is_open()) {
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::main), warn,
                          "Profile: cannot open profile file: {}", output_file_path);
      return;
    }

    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::main), debug,
                        "Profile: write heap profile result to: {}", output_file_path);
    output_file << result.value();
    output_file.close();

    flush_timer_->enableTimer(period_);
  });

  flush_timer_->enableTimer(period_);
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::main), info,
                      "Profile: tcmalloc heap profiler is started");
  heap_profile_started_ = true;
  return true;
}

bool TcmallocHeapProfiler::stopProfiler() {
  if (!heap_profile_started_) {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::main), warn,
                        "Profile: tcmalloc heap profiler was stopped");

    return false;
  }

  heap_file_path_ = "";
  period_ = {};
  flush_timer_->disableTimer();
  flush_timer_.reset();
  heap_profile_started_ = false;

  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::main), info,
                      "Profile: tcmalloc heap profiler is stopped");

  return true;
}

bool Cpu::profilerEnabled() { return false; }
bool Cpu::startProfiler(const std::string&) { return false; }
void Cpu::stopProfiler() {}

bool Heap::profilerEnabled() { return true; }
bool Heap::isProfilerStarted() { return tcmallocHeapProfiler().heapProfilerStarted(); }
bool Heap::startProfiler(const std::string& path) {
  // Output heap profile file every minute by default.
  // TODO(wbpcode): make this period configurable.
  return tcmallocHeapProfiler().startProfiler(path, std::chrono::milliseconds(60000));
}
bool Heap::stopProfiler() { return tcmallocHeapProfiler().stopProfiler(); }

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

#endif // #ifdef TCMALLOC
