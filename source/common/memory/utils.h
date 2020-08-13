#pragma once

#include "absl/types/optional.h"

namespace Envoy {
namespace Memory {

class Utils {
public:
  static void releaseFreeMemory();
  // Release free memory if the memory to free is greater than the threshold.
  // The threshold is fetched from runtime.
  static void tryShrinkHeap();
};

} // namespace Memory
} // namespace Envoy
