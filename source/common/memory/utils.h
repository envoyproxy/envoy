#pragma once

namespace Envoy {
namespace Memory {

class Utils {
public:
  static void releaseFreeMemory();
  static void tryShrinkHeap();
};

} // namespace Memory
} // namespace Envoy
