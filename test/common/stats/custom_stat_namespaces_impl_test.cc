#include "source/common/stats/custom_stat_namespaces_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

TEST(CustomStatNamespacesImpl, Registration) {
  CustomStatNamespacesImpl namespaces;
  const std::string name = "foo";
  EXPECT_FALSE(namespaces.registered(name));
  namespaces.registerStatNamespace(name);
  EXPECT_TRUE(namespaces.registered(name));
  EXPECT_FALSE(namespaces.registered("bar"));
}

TEST(CustomStatNamespacesImpl, StripRegisteredPrefix) {
  CustomStatNamespacesImpl namespaces;
  // no namespace is registered.
  EXPECT_FALSE(namespaces.stripRegisteredPrefix("foo.bar").has_value());
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

TEST(CustomStatNamespacesImpl, StripRegisteredInnerNamespace) {
  CustomStatNamespacesImpl namespaces;

  // Empty registry.
  EXPECT_FALSE(namespaces.stripRegisteredInnerNamespace("cluster.foo.metric").has_value());

  namespaces.registerStatNamespace("wasmcustom");

  // Depth 1.
  {
    const absl::optional<std::string> actual =
        namespaces.stripRegisteredInnerNamespace("cluster.wasmcustom.upstream_rq_2xx");
    EXPECT_TRUE(actual.has_value());
    EXPECT_EQ(actual.value(), "cluster.upstream_rq_2xx");
  }

  // Depth 2.
  {
    const absl::optional<std::string> actual =
        namespaces.stripRegisteredInnerNamespace("scope.subscope.wasmcustom.metric");
    EXPECT_TRUE(actual.has_value());
    EXPECT_EQ(actual.value(), "scope.subscope.metric");
  }

  // Multi-token leaf.
  {
    const absl::optional<std::string> actual =
        namespaces.stripRegisteredInnerNamespace("cluster.wasmcustom.foo.bar.baz");
    EXPECT_TRUE(actual.has_value());
    EXPECT_EQ(actual.value(), "cluster.foo.bar.baz");
  }

  // Leading match: caller should use stripRegisteredPrefix instead.
  EXPECT_FALSE(namespaces.stripRegisteredInnerNamespace("wasmcustom.metric").has_value());

  // Trailing match: that's the metric leaf, not a namespace boundary.
  EXPECT_FALSE(namespaces.stripRegisteredInnerNamespace("cluster.foo.wasmcustom").has_value());

  // No registered segment in the middle.
  EXPECT_FALSE(namespaces.stripRegisteredInnerNamespace("cluster.foo.bar.metric").has_value());

  // Substring within a segment doesn't count.
  EXPECT_FALSE(namespaces.stripRegisteredInnerNamespace("cluster.wasmcustomx.metric").has_value());

  // No dot at all.
  EXPECT_FALSE(namespaces.stripRegisteredInnerNamespace("wasmcustom").has_value());
}

} // namespace Stats
} // namespace Envoy
