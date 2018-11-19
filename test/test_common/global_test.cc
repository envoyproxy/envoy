#include <string>
#include <vector>

#include "test/test_common/global.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Test {

class GlobalTest : public testing::Test {
protected:
};

TEST_F(GlobalTest, SingletonStringAndVector) {
  {
    Global<std::string> s1;
    Global<std::vector<int>> v1;
    EXPECT_EQ("", *s1);
    *s1 = "foo";
    EXPECT_TRUE(v1->empty());
    v1->push_back(42);

    Global<std::string> s2;
    Global<std::vector<int>> v2;
    EXPECT_EQ("foo", *s2);
    ASSERT_EQ(1, v2->size());
    EXPECT_EQ(42, (*v2)[0]);
  }

  // After the globals went out of scope, referencing them again we start
  // from clean objects;
  Global<std::string> s3;
  Global<std::vector<int>> v3;
  EXPECT_EQ("", *s3);
  EXPECT_TRUE(v3->empty());
}

} // namespace Test
} // namespace Envoy
