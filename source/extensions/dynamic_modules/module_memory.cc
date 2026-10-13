#include "source/extensions/dynamic_modules/module_memory.h"

#if defined(__GLIBC__)
#include <dlfcn.h>
#include <link.h>
#endif

#include <algorithm>
#include <optional>
#include <vector>

#include "envoy/common/exception.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

namespace {

// A return address points past its call instruction, so the byte before it lies in the caller.
uintptr_t callSite(const void* return_address) {
  return reinterpret_cast<uintptr_t>(return_address) - 1;
}

bool contains(const std::vector<AddressRange>& ranges, uintptr_t address) {
  return std::any_of(ranges.begin(), ranges.end(), [address](const AddressRange& range) {
    return address >= range.begin && address < range.end;
  });
}

#if defined(__GLIBC__)
bool isAbiCallbackAddress(uintptr_t address) {
  Dl_info info;
  void* symbol = nullptr;
  if (dladdr1(reinterpret_cast<const void*>(address), &info, &symbol, RTLD_DL_SYMENT) == 0 ||
      info.dli_sname == nullptr || symbol == nullptr) {
    return false;
  }
  return absl::StartsWith(info.dli_sname, "envoy_dynamic_module_callback_") &&
         address < reinterpret_cast<uintptr_t>(info.dli_saddr) +
                       static_cast<const ElfW(Sym)*>(symbol)->st_size;
}
#else
bool isAbiCallbackAddress(uintptr_t) { return false; }
#endif

} // namespace

ModuleMemoryReader::ModuleMemoryReader(HeapSampleSource heap_sample_source)
    : heap_sample_source_(std::move(heap_sample_source)) {}

absl::StatusOr<uint64_t> ModuleMemoryReader::allocatedBytes(absl::string_view module_name) {
  // A shared object loaded under several names can appear in both lists, and its samples then count
  // for the measured module.
  std::vector<AddressRange> measured_code;
  std::vector<AddressRange> other_code;
  uint64_t allocated_bytes = 0;
  absl::Status status;
  forEachLoadedModule([&](const DynamicModule& module) {
    const std::vector<AddressRange>& ranges = module.codeRanges();
    if (module.name() != module_name) {
      other_code.insert(other_code.end(), ranges.begin(), ranges.end());
      return;
    }
    if (const std::optional<uint64_t> reported_bytes = module.reportedMemoryUsage();
        reported_bytes.has_value()) {
      allocated_bytes += *reported_bytes;
      // The module counts its own allocations, so its frames must not be attributed again.
      other_code.insert(other_code.end(), ranges.begin(), ranges.end());
      return;
    }
    if (ranges.empty()) {
      status = absl::UnimplementedError(
          absl::StrCat("Cannot estimate the heap memory of dynamic module ", module_name));
    }
    measured_code.insert(measured_code.end(), ranges.begin(), ranges.end());
  });
  RETURN_IF_NOT_OK_REF(status);
  if (measured_code.empty()) {
    return allocated_bytes;
  }

  RETURN_IF_NOT_OK(
      heap_sample_source_([&](absl::Span<void* const> stack, uint64_t requested_bytes) {
        for (size_t i = 0; i < stack.size(); ++i) {
          const uintptr_t address = callSite(stack[i]);
          if (contains(measured_code, address)) {
            if (i == 0 || !isAbiCallbackFrame(stack[i - 1])) {
              allocated_bytes += requested_bytes;
            }
            return;
          }
          if (contains(other_code, address)) {
            return;
          }
        }
      }));
  return allocated_bytes;
}

bool ModuleMemoryReader::isAbiCallbackFrame(const void* frame) {
  auto [it, inserted] = abi_callback_frames_.try_emplace(frame, false);
  if (inserted) {
    it->second = isAbiCallbackAddress(callSite(frame));
  }
  return it->second;
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
