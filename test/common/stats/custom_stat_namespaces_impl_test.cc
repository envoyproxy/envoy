#include "source/common/stats/custom_stat_namespaces_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

TEST(CustomStatNamespacesImpl, Registration) {
  CustomStatNamespacesImpl namespaces;
  EXPECT_TRUE(namespaces.empty());
  const std::string name = "foo";
  EXPECT_FALSE(namespaces.registered(name));
  namespaces.registerStatNamespace(name);
  EXPECT_TRUE(namespaces.registered(name));
  EXPECT_FALSE(namespaces.empty());
  EXPECT_FALSE(namespaces.registered("bar"));
}

TEST(CustomStatNamespacesImpl, StripRegisteredPrefix) {
  CustomStatNamespacesImpl namespaces;
  namespaces.registerStatNamespace("foo");
  // namespace is not registered.
  EXPECT_FALSE(namespaces.stripRegisteredPrefix("bar.my.value").has_value());
  EXPECT_FALSE(namespaces.stripRegisteredPrefix("foobar.my.value").has_value());
  // "." is not present in the stat name - we skip these cases.
  EXPECT_FALSE(namespaces.stripRegisteredPrefix("foo").has_value());
  EXPECT_FALSE(namespaces.stripRegisteredPrefix("bar").has_value());
  // Should be stripped.
  const absl::optional<absl::string_view> actual =
      namespaces.stripRegisteredPrefix("foo.my.extension.metric");
  EXPECT_TRUE(actual.has_value());
  EXPECT_EQ(actual.value(), "my.extension.metric");
}

} // namespace Stats
} // namespace Envoy
