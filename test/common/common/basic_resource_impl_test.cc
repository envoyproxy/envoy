#include <limits>

#include "common/common/basic_resource_impl.h"

#include "test/mocks/runtime/mocks.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {

class BasicResourceLimitImplTest : public testing::Test {
protected:
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(BasicResourceLimitImplTest, NoArgsConstructorVerifyMax) {
  BasicResourceLimitImpl br;

  EXPECT_EQ(br.max(), std::numeric_limits<uint64_t>::max());
}

TEST_F(BasicResourceLimitImplTest, VerifySetClearMax) {
  BasicResourceLimitImpl br(123);

  EXPECT_EQ(br.max(), 123);
  br.setMax(321);
  EXPECT_EQ(br.max(), 321);
  br.resetMax();
  EXPECT_EQ(br.max(), std::numeric_limits<uint64_t>::max());
}

TEST_F(BasicResourceLimitImplTest, IncDecCount) {
  BasicResourceLimitImpl br;

  EXPECT_EQ(br.count(), 0);
  br.inc();
  EXPECT_EQ(br.count(), 1);
  br.inc();
  br.inc();
  EXPECT_EQ(br.count(), 3);
  br.dec();
  EXPECT_EQ(br.count(), 2);
  br.decBy(2);
  EXPECT_EQ(br.count(), 0);
}

TEST_F(BasicResourceLimitImplTest, CanCreate) {
  BasicResourceLimitImpl br(2);

  EXPECT_TRUE(br.canCreate());
  br.inc();
  EXPECT_TRUE(br.canCreate());
  br.inc();
  EXPECT_FALSE(br.canCreate());
  br.dec();
  EXPECT_TRUE(br.canCreate());
  br.dec();
}

TEST_F(BasicResourceLimitImplTest, RuntimeMods) {
  BasicResourceLimitImpl br(1337, runtime_, "trololo");

  EXPECT_CALL(runtime_.snapshot_, getInteger("trololo", 1337)).WillOnce(Return(555));
  EXPECT_EQ(br.max(), 555);

  EXPECT_CALL(runtime_.snapshot_, getInteger("trololo", 1337)).WillOnce(Return(1337));
  EXPECT_EQ(br.max(), 1337);
}

} // namespace Envoy
