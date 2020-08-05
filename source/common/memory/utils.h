#pragma once

#include "absl/types/optional.h"

namespace Envoy {
namespace Memory {

class Utils {
public:
  static void releaseFreeMemory();
  // Release free memory if the memory to free is greater than the threshold.
  static void tryShrinkHeap(absl::optional<uint64_t> threshold);
};

} // namespace Memory
} // namespace Envoy
