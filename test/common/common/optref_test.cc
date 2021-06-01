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

class Foo {};
class Bar : public Foo {};

TEST(OptRefTest, Conversion) {
  Foo foo;
  Bar bar;
  OptRef<Foo> foo_ref(foo);
  OptRef<Bar> bar_ref(bar);

  // Copy construct conversion.
  OptRef<Foo> converted_ref(bar);
  OptRef<Foo> converted_optref(bar_ref);

  // Assignment conversion.
  foo_ref = bar;
  foo_ref = bar_ref;
}

} // namespace Envoy
