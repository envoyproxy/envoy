#include "envoy/formatter/http_formatter_context.h"

#include "source/common/formatter/http_specific_formatter.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Formatter {
namespace {

class TestContextExtension : public Context::Extension {
public:
  TestContextExtension(absl::string_view data) : data_(data) {}
  std::string data_;
};
class FakeContextExtension : public Context::Extension {};

TEST(Context, Context) {
  {
    TestContextExtension extension("test");

    Context context;
    EXPECT_TRUE(!context.extension().has_value());

    context.setExtension(extension);
    EXPECT_TRUE(!context.typedExtension<FakeContextExtension>().has_value());

    auto typed_extension = context.typedExtension<TestContextExtension>();
    EXPECT_TRUE(typed_extension.has_value());
    EXPECT_EQ(typed_extension->data_, "test");
  }
}

} // namespace
} // namespace Formatter
} // namespace Envoy
