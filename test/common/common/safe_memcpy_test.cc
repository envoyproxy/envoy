#include <array>

#include "common/common/safe_memcpy.h"
#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ElementsAre;

namespace Envoy {

TEST(SafeMemcpyTest, CopyUint8) {
  const uint8_t src[] = {0, 1, 1, 2, 3, 5, 8, 13};
  uint64_t dst;
  safeMemcpy(&dst, &src);
  EXPECT_EQ(dst, 282583128934413);
}

TEST(SafeMemcpyUnsafeSrcTest, CopyUint8Pointer) {
  uint8_t* src = new uint8_t[8];
  for (int i = 0; i < 8; ++i) {
    src[i] = i;
  }
  uint8_t dst[8];
  safeMemcpyUnsafeSrc(&dst, src);
  EXPECT_THAT(dst, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
  delete[] src;
}

TEST(SafeMemcpyUnsafeDstTest, PrependGrpcFrameHeader) {
  uint8_t* dst = new uint8_t[8];
  uint8_t src[8];
  for (int i = 0; i < 8; ++i) {
    src[i] = i;
  }
  safeMemcpyUnsafeSrc(dst, &src);
  for (int i = 0; i < 8; ++i) {
    EXPECT_THAT(dst[i], i);
  }
  delete[] dst;
}

} // namespace Envoy
