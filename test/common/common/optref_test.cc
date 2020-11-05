#include <string>

#include "envoy/common/optref.h"

#include "gtest/gtest.h"

namespace Envoy {

// Helper function for returning the string reference from an OptRef. Calling
// value() inline at the EXPECT_EQ callsites does not compile due to template
// specialization ambiguities, that this wrapper resolves.
static std::string& strref(const OptRef<std::string> optref) { return optref.value(); }

TEST(OptRefTest, Empty) {
  OptRef<std::string> optref;
  EXPECT_FALSE(optref.has_value());
}

TEST(OptRefTest, NonConst) {
  std::string str("Hello");
  OptRef<std::string> optref(str);
  EXPECT_TRUE(optref.has_value());
  EXPECT_EQ("Hello", strref(optref));
  EXPECT_EQ(5, optref->size());
  optref->append(", World!");
  EXPECT_EQ("Hello, World!", strref(optref));
}

TEST(OptRefTest, Const) {
  std::string str("Hello");
  const OptRef<std::string> optref(str);
  EXPECT_TRUE(optref.has_value());
  EXPECT_EQ("Hello", strref(optref));
  EXPECT_EQ(5, optref->size());
}

} // namespace Envoy
