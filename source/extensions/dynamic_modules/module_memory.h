#pragma once

#include <cstdint>
#include <functional>

#include "source/common/profiler/profiler.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * Reads the heap memory allocated by dynamic modules.
 *
 * The heap memory of a module is estimated from the samples of the heap profile. A sample belongs
 * to the module that owns the innermost frame of its allocation stack within the code of any
 * loaded module, unless that frame called an Envoy ABI callback, which then allocated the memory
 * on behalf of the module. This relies on the frames of ABI callbacks, which abi.h keeps on the
 * stack when Envoy is built with clang.
 *
 * This class is not thread-safe.
 */
class ModuleMemoryReader {
public:
  using HeapSampleSource =
      std::function<absl::Status(absl::FunctionRef<void(absl::Span<void* const>, uint64_t)>)>;

  /**
   * @param heap_sample_source visits the samples of the current heap profile.
   */
  explicit ModuleMemoryReader(
      HeapSampleSource heap_sample_source = Profiler::TcmallocProfiler::forEachHeapSample);

  /**
   * @param module_name the name the modules were loaded with.
   * @return the estimated heap bytes allocated by the modules loaded with the given name. Zero if
   * no such module is loaded, or an error if the estimate is not available for one of them.
   */
  absl::StatusOr<uint64_t> allocatedBytes(absl::string_view module_name);

private:
  bool isAbiCallbackFrame(const void* frame);

  const HeapSampleSource heap_sample_source_;
  // ABI callbacks are defined in the Envoy binary, which is never unloaded, so whether a frame lies
  // in one is resolved once.
  absl::flat_hash_map<const void*, bool> abi_callback_frames_;
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
