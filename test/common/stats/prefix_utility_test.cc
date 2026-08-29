#include <string>
#include <vector>

#include "envoy/stats/stats_macros.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/prefix_utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class MergeStatPrefixTest : public testing::Test {
protected:
  // Emits "<prefix>.name" the way POOL_COUNTER_PREFIX would, for byte-identical comparison.
  std::string legacyName(absl::string_view prefix) { return statPrefixJoin(prefix, "name"); }

  // The flat name the tagged prefix produces for a "name" leaf (must match legacyName()).
  std::string taggedName(const TaggedStatName& p) {
    return Utility::counterFromTaggedPrefix(scope_, p.baseName(), p.tags(), p.name(), "name")
        .name();
  }

  std::string base(const TaggedStatName& p) { return symbol_table_.toString(p.baseName()); }
  std::string prefix(const TaggedStatName& p) { return symbol_table_.toString(p.name()); }

  std::vector<std::pair<std::string, std::string>> tags(const TaggedStatName& p) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const StatNameTag& t : p.tags()) {
      out.emplace_back(symbol_table_.toString(t.first), symbol_table_.toString(t.second));
    }
    return out;
  }

  IsolatedStoreImpl store_;
  Scope& scope_{*store_.rootScope()};
  SymbolTable& symbol_table_{store_.symbolTable()};
  const std::string& hcm_tag_{Envoy::Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX};
  const std::string& cluster_tag_{Envoy::Config::TagNames::get().CLUSTER_NAME};
};

// Parent "http.<hcm>." + a literal own prefix; parent extracted as HCM tag, no own tag.
TEST_F(MergeStatPrefixTest, ParentPlusLiteralSelf) {
  const auto p = mergeStatPrefix(symbol_table_, "http.ingress.", "fault", {}, "fault");
  EXPECT_EQ(taggedName(p), "http.ingress.fault.name");
  EXPECT_EQ(taggedName(p), legacyName("http.ingress.fault."));
  EXPECT_EQ(base(p), "http.fault");
  EXPECT_THAT(tags(p), testing::ElementsAre(std::make_pair(hcm_tag_, "ingress")));
}

// Parent + "<own>." + an own-tag value (e.g. ext_authz's filter_stats_prefix).
TEST_F(MergeStatPrefixTest, ParentPlusSelfTag) {
  const auto p =
      mergeStatPrefix(symbol_table_, "http.ingress.", "ext_authz",
                      {{Envoy::Config::TagNames::get().EXT_AUTHZ_PREFIX, "waf"}}, "ext_authz.waf");
  EXPECT_EQ(taggedName(p), "http.ingress.ext_authz.waf.name");
  EXPECT_EQ(taggedName(p), legacyName("http.ingress.ext_authz.waf."));
  EXPECT_EQ(base(p), "http.ext_authz");
  EXPECT_THAT(tags(p), testing::ElementsAre(
                           std::make_pair(hcm_tag_, "ingress"),
                           std::make_pair(Envoy::Config::TagNames::get().EXT_AUTHZ_PREFIX, "waf")));
}

// Value-first own prefix, parent carried by the scope (empty parent -> no parent tag).
TEST_F(MergeStatPrefixTest, ValueFirstSelfNoParent) {
  const auto p =
      mergeStatPrefix(symbol_table_, "", "http_local_rate_limit",
                      {{Envoy::Config::TagNames::get().LOCAL_HTTP_RATELIMIT_PREFIX, "myprefix"}},
                      "myprefix.http_local_rate_limit");
  EXPECT_EQ(taggedName(p), "myprefix.http_local_rate_limit.name");
  EXPECT_EQ(taggedName(p), legacyName("myprefix.http_local_rate_limit"));
  EXPECT_EQ(base(p), "http_local_rate_limit");
  EXPECT_THAT(tags(p),
              testing::ElementsAre(std::make_pair(
                  Envoy::Config::TagNames::get().LOCAL_HTTP_RATELIMIT_PREFIX, "myprefix")));
}

// Cluster-scoped parent "cluster.<name>." extracted as CLUSTER_NAME tag.
TEST_F(MergeStatPrefixTest, ClusterParent) {
  const auto p = mergeStatPrefix(symbol_table_, "cluster.foo.", "ext_authz", {}, "ext_authz");
  EXPECT_EQ(taggedName(p), "cluster.foo.ext_authz.name");
  EXPECT_EQ(taggedName(p), legacyName("cluster.foo.ext_authz."));
  EXPECT_EQ(base(p), "cluster.ext_authz");
  EXPECT_THAT(tags(p), testing::ElementsAre(std::make_pair(cluster_tag_, "foo")));
}

// Cluster parent combined with an own tag: both the parent tag and the own tag are emitted, parent
// first, and both prefixes are tag-extracted.
TEST_F(MergeStatPrefixTest, ClusterParentWithSelfTag) {
  const auto p =
      mergeStatPrefix(symbol_table_, "cluster.foo.", "ext_authz",
                      {{Envoy::Config::TagNames::get().EXT_AUTHZ_PREFIX, "waf"}}, "ext_authz.waf");
  EXPECT_EQ(taggedName(p), "cluster.foo.ext_authz.waf.name");
  EXPECT_EQ(taggedName(p), legacyName("cluster.foo.ext_authz.waf."));
  EXPECT_EQ(base(p), "cluster.ext_authz");
  EXPECT_THAT(tags(p), testing::ElementsAre(
                           std::make_pair(cluster_tag_, "foo"),
                           std::make_pair(Envoy::Config::TagNames::get().EXT_AUTHZ_PREFIX, "waf")));
}

// A parent passed without a trailing dot will not be recognized as a tag because we assume
// the caller should handle the dot correctly.
TEST_F(MergeStatPrefixTest, ParentWithoutTrailingDot) {
  const auto p = mergeStatPrefix(symbol_table_, "http.ingress", "fault", {}, "fault");
  EXPECT_EQ(taggedName(p), "http.ingressfault.name");
  EXPECT_EQ(base(p), "http.ingressfault");
  EXPECT_EQ(prefix(p), "http.ingressfault");
  EXPECT_TRUE(tags(p).empty());
}

// The whole segment after the "http."/"cluster." root becomes the tag value (dots included).
TEST_F(MergeStatPrefixTest, MultiSegmentParentValue) {
  const auto p = mergeStatPrefix(symbol_table_, "http.a.b.", "fault", {}, "fault");
  EXPECT_EQ(taggedName(p), "http.a.b.fault.name");
  EXPECT_EQ(base(p), "http.fault");
  EXPECT_THAT(tags(p), testing::ElementsAre(std::make_pair(hcm_tag_, "a.b")));
}

// An unrecognized parent root is passed through untagged.
TEST_F(MergeStatPrefixTest, UnrecognizedParentNotTagged) {
  const auto p = mergeStatPrefix(symbol_table_, "listener.foo.", "fault", {}, "fault");
  EXPECT_EQ(taggedName(p), "listener.foo.fault.name");
  EXPECT_EQ(base(p), "listener.foo.fault");
  EXPECT_TRUE(tags(p).empty());
}

// With no own tags, the tagged own prefix is ignored and base_prefix drives both forms.
TEST_F(MergeStatPrefixTest, NoSelfTagsIgnoresSelfPrefix) {
  const auto p = mergeStatPrefix(symbol_table_, "http.ingress.", "fault", {}, "");
  EXPECT_EQ(taggedName(p), "http.ingress.fault.name");
  EXPECT_EQ(base(p), "http.fault");
  EXPECT_THAT(tags(p), testing::ElementsAre(std::make_pair(hcm_tag_, "ingress")));
}

// Empty parent and empty own prefix: stats sit at the root with no prefix.
TEST_F(MergeStatPrefixTest, EmptyParentAndSelf) {
  const auto p = mergeStatPrefix(symbol_table_, "", "", {}, "");
  EXPECT_EQ(taggedName(p), "name");
  EXPECT_EQ(base(p), "");
  EXPECT_TRUE(tags(p).empty());
}

// Empty own prefix with a tagged parent: stats sit directly under the parent.
TEST_F(MergeStatPrefixTest, EmptySelfWithParent) {
  const auto p = mergeStatPrefix(symbol_table_, "http.ingress.", "", {}, "");
  EXPECT_EQ(taggedName(p), "http.ingress.name");
  EXPECT_EQ(base(p), "http");
  EXPECT_THAT(tags(p), testing::ElementsAre(std::make_pair(hcm_tag_, "ingress")));
}

} // namespace
} // namespace Stats
} // namespace Envoy
