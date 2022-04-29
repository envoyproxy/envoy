#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <iostream>

#include "source/common/common/logger.h"
#include "source/server/backtrace.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {

namespace {

std::string captureBaseOffset(bool log_to_stderr) {
  vm_region_basic_info_data_t info;
  mach_vm_size_t size;
  mach_port_t object_name;
  mach_msg_type_number_t count;
  mach_vm_address_t address = 1;
  kern_return_t rc = mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO,
                                    (vm_region_info_t)&info, &count, &object_name);

  if (rc != KERN_SUCCESS) {
    if (log_to_stderr) {
      std::cerr << "Failed to query mach_vm_region to find base address (rc: " << rc << ")\n";
    } else {
      ENVOY_LOG_MISC(critical, "Failed to query mach_vm_region to find base address (rc: {})", rc);
    }
    return "<unknown:error>";
  }

  return absl::StrCat("0x", absl::Hex(address));
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
