#include <array>

#include "common/common/safe_memcpy.h"
#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {

TEST(SafeMemcpyTest, CopyUint8) {

  const uint8_t src[] = {0, 1, 1, 2, 3, 5, 8, 13};
  std::array<uint8_t, 8> dst;
  safeMemcpy(reinterpret_cast<uint8_t(*)[8]>(dst.data()), &src);
  Eq(dst == std::array<uint8_t, 8>{0, 1, 1, 2, 3, 5, 8, 13});
}

// Additional (integration) test - not ordinary copy
TEST(SafeMemcpyTest, PrependGrpcFrameHeader) {

  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add("test", 4);
  std::array<char, 5> expected_header;
  expected_header[0] = 0; // flags
  const uint32_t nsize = htonl(4);
  safeMemcpy(reinterpret_cast<char(*)[4]>(&expected_header[1]), &nsize);
  std::string header_string(&expected_header[0], 5);
  Grpc::Common::prependGrpcFrameHeader(*buffer);
  EXPECT_EQ(buffer->toString(), header_string + "test");
}

} // namespace Envoy
