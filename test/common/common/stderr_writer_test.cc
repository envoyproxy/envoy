#include <cstdint>
#include <string>

#include "source/common/common/stderr_writer.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(WriteStderrTest, FormatsValues) {
  const void* address = reinterpret_cast<const void*>(uintptr_t{0x1f2e3d});
  testing::internal::CaptureStderr();
  writeStderr("Caught %s, #%d: [%p]\n", "Segmentation fault", 7, address);
  EXPECT_EQ("Caught Segmentation fault, #7: [0x1f2e3d]\n", testing::internal::GetCapturedStderr());
}

TEST(WriteStderrTest, TruncatesLongOutput) {
  const std::string text(8192, 'x');
  testing::internal::CaptureStderr();
  writeStderr("%s\n", text);
  EXPECT_EQ(std::string(4607, 'x'), testing::internal::GetCapturedStderr());
}

TEST(WriteStderrTest, WritesFrames) {
  const void* address = reinterpret_cast<const void*>(uintptr_t{0x1f2e3d});
  testing::internal::CaptureStderr();
  writeStderrFrame(3, "Envoy::main()", address);
  writeStderrFrame(4, nullptr, address);
  EXPECT_EQ("[backtrace] [stderr] #3: Envoy::main() [0x1f2e3d]\n"
            "[backtrace] [stderr] #4: [0x1f2e3d]\n",
            testing::internal::GetCapturedStderr());
}

} // namespace
} // namespace Envoy
