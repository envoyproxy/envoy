// Note: this should be run with --compilation_mode=opt.

#include <cstdint>
#include <filesystem>
#include <vector>

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/module_memory.h"

#include "test/benchmark/main.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Measures one estimate of the heap memory of a module that holds the given number of live 4 MiB
// blocks, each of which is about one sample of the heap profile.
static void bmAllocatedBytes(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(0) > 16) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }
  // Benchmarks run in their runfiles directory.
  absl::StatusOr<DynamicModulePtr> module =
      newDynamicModule(std::filesystem::absolute(
                           "test/extensions/dynamic_modules/test_data/c/libmemory_allocator.so"),
                       false, false, "measured");
  if (!module.ok()) {
    state.SkipWithError(module.status().ToString());
    return;
  }
  auto allocate = (*module)->getFunctionPointer<void* (*)(size_t)>("allocate_memory").value();
  auto release = (*module)->getFunctionPointer<void (*)(void*)>("free_memory").value();
  std::vector<void*> blocks;
  for (int64_t i = 0; i < state.range(0); ++i) {
    blocks.push_back(allocate(4 << 20));
  }

  ModuleMemoryReader reader;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    ::benchmark::DoNotOptimize(reader.allocatedBytes("measured"));
  }

  for (void* block : blocks) {
    release(block);
  }
}
BENCHMARK(bmAllocatedBytes)->Arg(16)->Arg(256)->Arg(1024)->Unit(::benchmark::kMicrosecond);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
