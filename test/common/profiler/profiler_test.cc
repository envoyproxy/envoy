#include <cstdint>
#include <cstdlib>

#include "source/common/profiler/profiler.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Profiler {
namespace {

#if defined(TCMALLOC)
TEST(TcmallocProfilerTest, ForEachHeapSampleVisitsLiveAllocations) {
  // An allocation much larger than the sampling interval of the heap profile is always sampled.
  // Storing it in a volatile keeps the compiler from eliding the allocation.
  constexpr uint64_t size = 64 << 20;
  void* volatile memory = malloc(size);
  ASSERT_NE(nullptr, memory);
  uint64_t sampled_bytes = 0;
  EXPECT_OK(TcmallocProfiler::forEachHeapSample(
      [&sampled_bytes](absl::Span<void* const> stack, uint64_t requested_bytes) {
        if (requested_bytes >= size && !stack.empty()) {
          sampled_bytes = requested_bytes;
        }
      }));
  free(memory);
  EXPECT_GE(sampled_bytes, size);
}
#else
TEST(TcmallocProfilerTest, ForEachHeapSampleIsUnimplemented) {
  EXPECT_THAT(TcmallocProfiler::forEachHeapSample([](absl::Span<void* const>, uint64_t) {}),
              StatusHelpers::HasStatusCode(absl::StatusCode::kUnimplemented));
}
#endif

} // namespace
} // namespace Profiler
} // namespace Envoy
