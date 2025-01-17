#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/buffer_util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

class UtilTest : public testing::Test {
protected:
  std::string serialize(double number) {
    OwnedImpl buf;
    Util::serializeDouble(number, buf);
    return buf.toString();
  }
};

TEST_F(UtilTest, SerializeDouble) {
  EXPECT_EQ("0", serialize(0));
  EXPECT_EQ("0.5", serialize(0.5));
  EXPECT_EQ("3.141592654", serialize(3.141592654));
  EXPECT_EQ("-989282.1087", serialize(-989282.1087));
  EXPECT_EQ("1.23456789012345e+67", serialize(1.23456789012345e+67));
}

} // namespace
} // namespace Buffer
} // namespace Envoy
