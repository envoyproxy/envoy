#include <iostream>

#include "envoy/common/platform.h"

#include "source/common/common/logger.h"
#include "source/server/backtrace.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {

namespace {

std::string captureBaseOffset(bool log_to_stderr) {
  HMODULE my_module = GetModuleHandle(NULL);
  if (my_module == NULL) {
    if (log_to_stderr) {
      std::cerr << "Failed to call GetModuleHandle(NULL) to find current base address.\n";
    } else {
      ENVOY_LOG_MISC(critical,
                     "Failed to call GetModuleHandle(NULL) to find current base address.");
    }
    return "<unknown:error>";
  }
  return absl::StrCat("0x", absl::Hex((uintptr_t)my_module));
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
