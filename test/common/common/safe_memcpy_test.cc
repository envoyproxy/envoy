#include <array>

#include "common/common/safe_memcpy.h"
#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::Eq;

namespace Envoy {

TEST(SafeMemcpyTest, CopyUint8) {
  const uint8_t src[] = {0, 1, 1, 2, 3, 5, 8, 13};
  uint64_t dst;
  safeMemcpy(&dst, &src);
  Eq(dst == 282583128934413);
}

TEST(SafeMemcpyUnsafeSrcTest, CopyUint8Pointer) {
  uint8_t* src = new uint8_t[8];
  for (int i = 0; i < 8; ++i) {
    src[i] = i;
  }
  uint8_t dst[8];
  safeMemcpyUnsafeSrc(&dst, src);
  ASSERT_THAT(dst, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
  delete[] src;
}

TEST(SafeMemcpyUnsafeDstTest, PrependGrpcFrameHeader) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add("test", 4);
  std::array<char, 5> expected_header;
  expected_header[0] = 0; // flags
  const uint32_t nsize = htonl(4);
  safeMemcpyUnsafeDst(&expected_header[1], &nsize);
  std::string header_string(&expected_header[0], 5);
  Grpc::Common::prependGrpcFrameHeader(*buffer);
  EXPECT_EQ(buffer->toString(), header_string + "test");
}

} // namespace Envoy
