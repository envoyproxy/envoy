#include <cstdint>
#include <stdexcept>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/sdk/cpp/sdk_internal_common.h"

#include "gtest/gtest.h"

// Stub the ABI logging callback the barrier calls on the catch path.
extern "C" void envoy_dynamic_module_callback_log_v2(envoy_dynamic_module_type_log_level,
                                                     envoy_dynamic_module_type_module_buffer,
                                                     envoy_dynamic_module_type_module_buffer,
                                                     uint32_t) {}

namespace Envoy {
namespace DynamicModules {
namespace {

TEST(FailClosedTest, ReturnsBodyValueWhenNoThrow) {
  EXPECT_EQ(7, failClosed("t", 42, []() -> int { return 7; }));
}

TEST(FailClosedTest, ReturnsFailValueOnThrow) {
  EXPECT_EQ(42, failClosed("t", 42, []() -> int { throw std::runtime_error("boom"); }));
}

TEST(FailClosedTest, CatchesNonStandardException) {
  EXPECT_EQ(5, failClosed("t", 5, []() -> int { throw 123; }));
}

TEST(FailClosedTest, PointerReturnFailsClosedToNull) {
  EXPECT_EQ(nullptr, failClosed("t", nullptr, []() -> int* { throw std::runtime_error("x"); }));
}

TEST(FailClosedTest, BoolFailValueIsReturnedOnThrow) {
  EXPECT_TRUE(failClosed("t", true, []() -> bool { throw std::runtime_error("x"); }));
}

TEST(FailClosedVoidTest, SwallowsExceptionAndDoesNotPropagate) {
  bool ran = false;
  failClosedVoid("t", [&]() {
    ran = true;
    throw std::runtime_error("x");
  });
  EXPECT_TRUE(ran);
}

TEST(FailClosedVoidTest, SwallowsNonStandardException) {
  bool ran = false;
  failClosedVoid("t", [&]() {
    ran = true;
    throw 123;
  });
  EXPECT_TRUE(ran);
}

} // namespace
} // namespace DynamicModules
} // namespace Envoy
