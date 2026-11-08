#include <string>
#include <vector>

#include "source/extensions/dynamic_modules/abi_context_accessors.h"

#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

envoy_dynamic_module_type_module_http_header moduleHeader(absl::string_view key,
                                                          absl::string_view value) {
  return {key.data(), key.size(), value.data(), value.size()};
}

TEST(ModuleHeaderKeyTest, RejectsWhatHttpDoesNotAllow) {
  for (const absl::string_view name :
       {absl::string_view(""), absl::string_view(":"), absl::string_view("bad name"),
        absl::string_view("a\rb"), absl::string_view("a\nb"), absl::string_view("a\0b", 3)}) {
    SCOPED_TRACE(testing::PrintToString(std::string(name)));
    EXPECT_FALSE(moduleHeaderKey(name).has_value());
  }
  for (const absl::string_view value :
       {absl::string_view("a\rb"), absl::string_view("a\nb"), absl::string_view("a\0b", 3),
        absl::string_view("bell\a")}) {
    SCOPED_TRACE(testing::PrintToString(std::string(value)));
    EXPECT_FALSE(moduleHeaderKey("x-key", value).has_value());
  }
}

TEST(ModuleHeaderKeyTest, AcceptsWhatHttpAllows) {
  EXPECT_EQ(moduleHeaderKey("X-Mixed-Case"), Http::LowerCaseString("x-mixed-case"));
  EXPECT_EQ(moduleHeaderKey(":path", "/a?b=c"), Http::LowerCaseString(":path"));
  EXPECT_EQ(moduleHeaderKey("x-key", "a\tb \x80\xff"), Http::LowerCaseString("x-key"));
  EXPECT_EQ(moduleHeaderKey("x-key", ""), Http::LowerCaseString("x-key"));
}

TEST(ModuleHeaderKeyTest, RuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_reject_invalid_headers", "false"}});
  // HTTP does not allow these bytes, but the header map's asserts do not catch them.
  EXPECT_EQ(moduleHeaderKey("Bad Name", "bell\a"), Http::LowerCaseString("bad name"));
}

TEST(AddModuleHeadersTest, SkipsInvalidHeaders) {
  Http::TestRequestHeaderMapImpl map;
  const std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      moduleHeader("x-valid", "value"), moduleHeader("x-bad", "value\r\nx-injected: 1"),
      moduleHeader("", "value")};
  EXPECT_FALSE(addModuleHeaders(map, headers.data(), headers.size()));
  EXPECT_EQ(map.size(), 1);
  EXPECT_EQ(map.get_("x-valid"), "value");

  EXPECT_TRUE(addModuleHeaders(map, headers.data(), 1));
  EXPECT_EQ(map.get(Http::LowerCaseString("x-valid")).size(), 2);
  EXPECT_TRUE(addModuleHeaders(map, nullptr, 0));
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
