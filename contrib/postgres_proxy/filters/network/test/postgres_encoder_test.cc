#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "contrib/postgres_proxy/filters/network/source/postgres_encoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class EncoderTest : public ::testing::Test {
public:
  EncoderTest() { encoder_ = std::make_unique<Encoder>(); }

protected:
  std::unique_ptr<Encoder> encoder_;
};

TEST_F(EncoderTest, BuildErrorResponseProducesCorrectFormat) {
  using namespace std::string_literals;
  Buffer::OwnedImpl data = encoder_->buildErrorResponse("FATAL", "some error", "28000");
  constexpr uint32_t header_size = 1 + sizeof(uint32_t);

  // Make sure response sent back to client is valid
  ASSERT_GE(data.length(), header_size);
  EXPECT_EQ(data.peekBEInt<char>(0), 'E');
  EXPECT_EQ(data.peekBEInt<uint32_t>(1), data.length() - 1);
  data.drain(header_size);
  EXPECT_EQ(data.toString(), "SFATAL\0Msome error\0C28000\0\0"s);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
