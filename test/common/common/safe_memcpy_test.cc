#include <vector>

#include "source/common/common/safe_memcpy.h"
#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ElementsAre;

namespace Envoy {

TEST(SafeMemcpyTest, CopyUint8) {
  const uint8_t src[] = {0, 1, 1, 2, 3, 5, 8, 13};
  uint8_t dst[8];
  safeMemcpy(&dst, &src);
  EXPECT_THAT(dst, ElementsAre(0, 1, 1, 2, 3, 5, 8, 13));
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
  const uint8_t src[] = {1, 2, 3, 4};
  uint8_t* dst = new uint8_t[4];
  memset(dst, 0, 4);
  safeMemcpyUnsafeDst(dst, &src);
  EXPECT_THAT(std::vector<uint8_t>(dst, dst + sizeof(src)), ElementsAre(1, 2, 3, 4));
  delete[] dst;
}

} // namespace Envoy
