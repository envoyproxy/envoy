#include "common/router/string_accessor_impl.h"

#include "absl/strings/string_view.h"
#include "test/test_common/test_base.h"

namespace Envoy {
namespace Router {

TEST(StringAccessorImplTest, Storage) {
  const char* const TestString = "test string 1";
  StringAccessorImpl accessor(TestString);

  EXPECT_EQ(TestString, accessor.asString());
}

} // namespace Router
} // namespace Envoy
