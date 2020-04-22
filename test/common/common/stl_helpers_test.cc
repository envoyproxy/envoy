#include <sstream>

#include "common/common/stl_helpers.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(StlHelpersTest, TestOutputToStreamOperator) {
  std::stringstream os;
  std::vector<int> v{1, 2, 3, 4, 5};
  os << v;
  EXPECT_EQ("vector { 1, 2, 3, 4, 5 }", os.str());
}

} // namespace Envoy
