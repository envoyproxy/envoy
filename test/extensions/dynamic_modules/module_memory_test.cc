#include <cstdint>
#include <utility>
#include <vector>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/module_memory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

using HeapSample = std::pair<std::vector<void*>, uint64_t>;

ModuleMemoryReader::HeapSampleSource fakeHeapProfile(std::vector<HeapSample> samples) {
  return [samples =
              std::move(samples)](absl::FunctionRef<void(absl::Span<void* const>, uint64_t)> fn) {
    for (const auto& [stack, requested_bytes] : samples) {
      fn(stack, requested_bytes);
    }
    return absl::OkStatus();
  };
}

// The return address of a call made from the start of the given function.
template <class Function> void* returnAddressIn(Function function) {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(function) + 1);
}

void envoyFunction() {}

DynamicModulePtr loadModule(const std::string& file_name, absl::string_view name) {
  absl::StatusOr<DynamicModulePtr> module =
      newDynamicModule(testSharedObjectPath(file_name, "c"), false, false, name);
  EXPECT_TRUE(module.ok()) << module.status();
  return std::move(module.value());
}

void* returnAddressInModule(const DynamicModule& module) {
  return returnAddressIn(module
                             .getFunctionPointer<decltype(&envoy_dynamic_module_on_program_init)>(
                                 "envoy_dynamic_module_on_program_init")
                             .value());
}

TEST(ModuleMemoryReaderTest, ModuleNotLoaded) {
  ModuleMemoryReader reader([](absl::FunctionRef<void(absl::Span<void* const>, uint64_t)>) {
    ADD_FAILURE() << "the heap profile must not be read for a module that is not loaded";
    return absl::OkStatus();
  });
  EXPECT_THAT(reader.allocatedBytes("not_loaded"), StatusHelpers::IsOkAndHolds(uint64_t{0}));
}

#if defined(__linux__)
TEST(ModuleMemoryReaderTest, AttributesSamplesToInnermostModuleFrame) {
  DynamicModulePtr measured = loadModule("memory_allocator", "measured");
  DynamicModulePtr other = loadModule("no_op", "other");
  void* const envoy_frame = returnAddressIn(&envoyFunction);
  void* const measured_frame = returnAddressInModule(*measured);
  void* const other_frame = returnAddressInModule(*other);
  void* const callback_frame = returnAddressIn(&envoy_dynamic_module_callback_log);

  ModuleMemoryReader reader(fakeHeapProfile({
      {{envoy_frame, measured_frame}, 1},
      {{envoy_frame, callback_frame, measured_frame}, 2},
      {{envoy_frame, envoy_frame}, 4},
      {{envoy_frame, other_frame, measured_frame}, 8},
      {{envoy_frame, measured_frame, other_frame}, 16},
      {{measured_frame}, 32},
      // The same callback frame again, so that the cached result is used.
      {{envoy_frame, callback_frame, measured_frame}, 64},
  }));
  EXPECT_THAT(reader.allocatedBytes("measured"),
              StatusHelpers::IsOkAndHolds(uint64_t{1 + 16 + 32}));
  EXPECT_THAT(reader.allocatedBytes("other"), StatusHelpers::IsOkAndHolds(uint64_t{8}));
}

TEST(ModuleMemoryReaderTest, SharedObjectLoadedUnderSeveralNames) {
  DynamicModulePtr first = loadModule("memory_allocator", "first");
  DynamicModulePtr second = loadModule("memory_allocator", "second");
  void* const envoy_frame = returnAddressIn(&envoyFunction);

  ModuleMemoryReader reader(fakeHeapProfile({{{envoy_frame, returnAddressInModule(*first)}, 1}}));
  EXPECT_THAT(reader.allocatedBytes("first"), StatusHelpers::IsOkAndHolds(uint64_t{1}));
  EXPECT_THAT(reader.allocatedBytes("second"), StatusHelpers::IsOkAndHolds(uint64_t{1}));
}

TEST(ModuleMemoryReaderTest, HeapProfileError) {
  DynamicModulePtr module = loadModule("memory_allocator", "measured");
  ModuleMemoryReader reader([](absl::FunctionRef<void(absl::Span<void* const>, uint64_t)>) {
    return absl::UnimplementedError("no heap profile");
  });
  EXPECT_THAT(reader.allocatedBytes("measured"),
              StatusHelpers::HasStatus(absl::StatusCode::kUnimplemented, "no heap profile"));
}
#endif

TEST(ModuleMemoryReaderTest, StaticModuleIsNotSupported) {
  absl::StatusOr<DynamicModulePtr> module = newDynamicModuleByName("matcher_no_op_static", false);
  ASSERT_TRUE(module.ok()) << module.status();
  ModuleMemoryReader reader(fakeHeapProfile({}));
  EXPECT_THAT(reader.allocatedBytes("matcher_no_op_static"),
              StatusHelpers::HasStatus(
                  absl::StatusCode::kUnimplemented,
                  "Cannot estimate the heap memory of dynamic module matcher_no_op_static"));
}

#if defined(TCMALLOC)
TEST(ModuleMemoryReaderTest, EstimatesHeapMemoryAllocatedByModule) {
  DynamicModulePtr module = loadModule("memory_allocator", "measured");
  auto allocate = module->getFunctionPointer<void* (*)(size_t)>("allocate_memory").value();
  auto release = module->getFunctionPointer<void (*)(void*)>("free_memory").value();
  ModuleMemoryReader reader;

  // An allocation much larger than the sampling interval of the heap profile is always sampled.
  constexpr uint64_t size = 64 << 20;
  void* memory = allocate(size);
  EXPECT_THAT(reader.allocatedBytes("measured"), StatusHelpers::IsOkAndHolds(testing::Ge(size)));
  release(memory);
  EXPECT_THAT(reader.allocatedBytes("measured"), StatusHelpers::IsOkAndHolds(uint64_t{0}));
}
#endif

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
