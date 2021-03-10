#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"

#include "common/buffer/buffer_impl.h"

#include "envoy/common/exception.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

namespace {

TEST(BufferHelperTest, BufferHelperTest) {
  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(BufferHelper::peekDouble(buffer, 0), EnvoyException,
                            "buffer underflow");

  EXPECT_THROW_WITH_MESSAGE(BufferHelper::peekFloat(buffer, 0), EnvoyException, "buffer underflow");

  double test_double = 0.0003;
  float test_float = 0.3;

  buffer.add(static_cast<void*>(&test_double), 8);
  buffer.add(static_cast<void*>(&test_float), 4);

  BufferHelper::peekDouble(buffer, 0);
  BufferHelper::peekFloat(buffer, 8);
}

} // namespace

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
