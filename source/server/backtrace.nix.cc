#include <array>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "source/common/common/logger.h"
#include "source/server/backtrace.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {

namespace {

std::unique_ptr<char, decltype(std::free)*> getExecutablePath() {
  return std::unique_ptr<char, decltype(std::free)*>{realpath("/proc/self/exe", nullptr),
                                                     std::free};
}

std::string captureBaseOffset(bool log_to_stderr_) {
  // Borrow a trick from abseil and read `/proc/self/task/{pid}/maps` instead of
  // `/proc/self/maps`. The latter requires the kernel to stop all threads, which
  // is significantly slower when there are many threads.
  std::array<char, 80> maps_path;
  snprintf(maps_path.data(), maps_path.size(), "/proc/self/task/%d/maps", getpid());
  std::ifstream maps_stream(maps_path.data());
  if (!maps_stream.is_open()) {
    if (log_to_stderr_) {
      std::cerr << "Failed to open (" << maps_path.data() << "), will not find base address\n";
    } else {
      ENVOY_LOG_MISC(critical, "Failed to open ({}), will not find base address", maps_path.data());
    }
    // prevent looking up again, we won't be able to open.
    return "<unknown:error>";
  }

  auto my_path = getExecutablePath();
  if (my_path == nullptr) {
    if (log_to_stderr_) {
      std::cerr << "Failed to get current executable's path!\n";
    } else {
      ENVOY_LOG_MISC(critical, "Failed to get current executable's path!");
    }
    return "<unknown:error>";
  }
  auto my_path_as_sv = absl::string_view(my_path.get());
  for (std::string line; std::getline(maps_stream, line);) {
    // We have a line from maps it looks something like this:
    // 08048000-0804c000 r-xp 00000000 08:01 2142121    /bin/cat
    //
    // We want to look for the data section of envoy so we need: `r-xp`.
    // Next we want to look for the active program name.
    std::vector<absl::string_view> mapped_line = absl::StrSplit(line, ' ');
    if (mapped_line.size() < 6) {
      // Invalid line?
      continue;
    }
    if (mapped_line[1] != "r-xp") {
      // Not executable data section.
      continue;
    }
    if (mapped_line.back() != my_path_as_sv) {
      // Not my executable!
      continue;
    }
    std::vector<absl::string_view> split_addresses = absl::StrSplit(mapped_line[0], '-');
    if (split_addresses.size() != 2) {
      // Corrupt line.
      continue;
    }
    return absl::StrCat("0x", split_addresses[0]);
  }

  if (log_to_stderr_) {
    std::cerr << "Failed to find start memory address, program line not found in ("
              << maps_path.data() << ")\n";
  } else {
    ENVOY_LOG_MISC(critical, "Failed to find start memory address, program line not found in ({})",
                   maps_path.data());
  }
  return "<unknown:error>";
}

ABSL_CONST_INIT static absl::Mutex kOffsetLock(absl::kConstInit);
ABSL_CONST_INIT std::string* baseAddress ABSL_GUARDED_BY(kOffsetLock) = nullptr;

} // namespace

bool BackwardsTrace::log_to_stderr_ = false;

void BackwardsTrace::setLogToStderr(bool log_to_stderr) { log_to_stderr_ = log_to_stderr; }

std::string BackwardsTrace::getBaseOffset() {
  // try to read cached.
  {
    absl::ReaderMutexLock l(&kOffsetLock);
    if (baseAddress != nullptr) {
      return *baseAddress;
    }
  }

  // Capture the base offset once.
  absl::WriterMutexLock l(&kOffsetLock);
  // Someone may have beaten us.
  if (baseAddress != nullptr) {
    return *baseAddress;
  }
  // We were the first, let's write.
  auto addr = captureBaseOffset(log_to_stderr_);
  baseAddress = new std::string(addr);
  return *baseAddress;
}

} // namespace Envoy
