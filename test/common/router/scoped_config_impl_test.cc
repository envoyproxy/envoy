#include <memory>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/router/scoped_config_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::Http::TestHeaderMapImpl;
using ::testing::NiceMock;

class FooFragment : public ScopeKeyFragmentBase {
public:
  uint64_t hash() const override { return 1; }
};

TEST(ScopeKeyFragmentBaseTest, EqualSign) {
  FooFragment foo;
  StringKeyFragment bar("a random string");

  EXPECT_NE(foo, bar);
}

TEST(ScopeKeyFragmentBaseTest, HashStable) {
  FooFragment foo1;
  FooFragment foo2;

  // Two FooFragments equal because their hash equals.
  EXPECT_EQ(foo1, foo2);
  EXPECT_EQ(foo1.hash(), foo2.hash());

  // Hash value doesn't change.
  StringKeyFragment a("abcdefg");
  auto hash_value = a.hash();
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(hash_value, a.hash());
    EXPECT_EQ(StringKeyFragment("abcdefg").hash(), hash_value);
  }
}

TEST(StringKeyFragmentTest, Empty) {
  StringKeyFragment a("");
  StringKeyFragment b("");
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.hash(), b.hash());

  StringKeyFragment non_empty("ABC");

  EXPECT_NE(a, non_empty);
  EXPECT_NE(a.hash(), non_empty.hash());
}

TEST(StringKeyFragmentTest, Normal) {
  StringKeyFragment str("Abc");

  StringKeyFragment same_str("Abc");
  EXPECT_EQ(str, same_str);

  StringKeyFragment upper_cased_str("ABC");
  EXPECT_NE(str, upper_cased_str);

  StringKeyFragment another_str("DEF");
  EXPECT_NE(str, another_str);
}

TEST(HeaderValueExtractorImplDeathTest, InvalidConfig) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  // Type not set, ASSERT only fails in debug mode.
#if !defined(NDEBUG)
  EXPECT_DEATH(HeaderValueExtractorImpl(std::move(config)), "header_value_extractor is not set.");
#else
  EXPECT_THROW_WITH_REGEX(HeaderValueExtractorImpl(std::move(config)), ProtoValidationException,
                          "HeaderValueExtractor extract_type not set.+");
#endif // !defined(NDEBUG)

  // Index non-zero when element separator is an empty string.
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
   index: 1
)EOF";
  TestUtility::loadFromYaml(yaml_plain, config);

  EXPECT_THROW_WITH_REGEX(HeaderValueExtractorImpl(std::move(config)), ProtoValidationException,
                          "Index > 0 for empty string element separator.");
  // extract_type not set.
  yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
)EOF";
  TestUtility::loadFromYaml(yaml_plain, config);

  EXPECT_THROW_WITH_REGEX(HeaderValueExtractorImpl(std::move(config)), ProtoValidationException,
                          "HeaderValueExtractor extract_type not set.+");
}

TEST(HeaderValueExtractorImplTest, HeaderExtractionByIndex) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ','
   index: 1
)EOF";

  TestUtility::loadFromYaml(yaml_plain, config);
  HeaderValueExtractorImpl extractor(std::move(config));
  std::unique_ptr<ScopeKeyFragmentBase> fragment =
      extractor.computeFragment(TestHeaderMapImpl{{"foo_header", "part-0,part-1:value_bluh"}});

  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"part-1:value_bluh"});

  // No such header.
  fragment = extractor.computeFragment(TestHeaderMapImpl{{"bar_header", "part-0"}});
  EXPECT_EQ(fragment, nullptr);

  // Empty header value.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", ""},
  });
  EXPECT_EQ(fragment, nullptr);

  // Index out of bound.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "part-0"},
  });
  EXPECT_EQ(fragment, nullptr);

  // Element is empty.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "part-0,,,bluh"},
  });
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment(""));
}

TEST(HeaderValueExtractorImplTest, HeaderExtractionByKey) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ';'
   element:
    key: 'bar'
    separator: '=>'
)EOF";

  TestUtility::loadFromYaml(yaml_plain, config);
  HeaderValueExtractorImpl extractor(std::move(config));
  std::unique_ptr<ScopeKeyFragmentBase> fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "part-0;bar=>bluh;foo=>foo_value"},
  });

  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"bluh"});

  // No such header.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"bluh", "part-0;"},
  });
  EXPECT_EQ(fragment, nullptr);

  // Empty header value.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", ""},
  });
  EXPECT_EQ(fragment, nullptr);

  // No such key.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "part-0"},
  });
  EXPECT_EQ(fragment, nullptr);

  // Empty value.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "bluh;;bar=>;foo=>last_value"},
  });
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{""});

  // Duplicate values, the first value returned.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "bluh;;bar=>value1;bar=>value2;bluh;;bar=>last_value"},
  });
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"value1"});

  // No separator in the element, value is set to empty string.
  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "bluh;;bar;bar=>value2;bluh;;bar=>last_value"},
  });
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{""});
}

TEST(HeaderValueExtractorImplTest, ElementSeparatorEmpty) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
   element:
    key: 'bar'
    separator: '='
)EOF";

  TestUtility::loadFromYaml(yaml_plain, config);
  HeaderValueExtractorImpl extractor(std::move(config));
  std::unique_ptr<ScopeKeyFragmentBase> fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "bar=b;c=d;e=f"},
  });
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"b;c=d;e=f"});

  fragment = extractor.computeFragment(TestHeaderMapImpl{
      {"foo_header", "a=b;bar=d;e=f"},
  });
  EXPECT_EQ(fragment, nullptr);
}

// Helper function which makes a ScopeKey from a list of strings.
ScopeKey makeKey(const std::vector<const char*>& parts) {
  ScopeKey key;
  for (const auto& part : parts) {
    key.addFragment(std::make_unique<StringKeyFragment>(part));
  }
  return key;
}

TEST(ScopeKeyDeathTest, AddNullFragment) {
  ScopeKey key;
#if !defined(NDEBUG)
  EXPECT_DEBUG_DEATH(key.addFragment(nullptr), "null fragment not allowed in ScopeKey.");
#endif
}

TEST(ScopeKeyTest, Unmatches) {
  ScopeKey key1;
  ScopeKey key2;
  // Empty key != empty key.
  EXPECT_NE(key1, key2);

  // Empty key != non-empty key.
  EXPECT_NE(key1, makeKey({""}));

  EXPECT_EQ(makeKey({"a", "b", "c"}), makeKey({"a", "b", "c"}));

  // Order matters.
  EXPECT_EQ(makeKey({"a", "b", "c"}), makeKey({"a", "b", "c"}));
  EXPECT_NE(makeKey({"a", "c", "b"}), makeKey({"a", "b", "c"}));

  // Two keys of different length won't match.
  EXPECT_NE(makeKey({"a", "b"}), makeKey({"a", "b", "c"}));

  // Case sensitive.
  EXPECT_NE(makeKey({"a", "b"}), makeKey({"A", "b"}));
}

TEST(ScopeKeyTest, Matches) {
  // An empty string fragment equals another.
  EXPECT_EQ(makeKey({"", ""}), makeKey({"", ""}));
  EXPECT_EQ(makeKey({"a", "", ""}), makeKey({"a", "", ""}));

  // Non empty fragments comparison.
  EXPECT_EQ(makeKey({"A", "b"}), makeKey({"A", "b"}));
}

TEST(ScopeKeyBuilderImplTest, Parse) {
  std::string yaml_plain = R"EOF(
  fragments:
  - header_value_extractor:
      name: 'foo_header'
      element_separator: ','
      element:
        key: 'bar'
        separator: '='
  - header_value_extractor:
      name: 'bar_header'
      element_separator: ';'
      index: 2
)EOF";

  ScopedRoutes::ScopeKeyBuilder config;
  TestUtility::loadFromYaml(yaml_plain, config);
  ScopeKeyBuilderImpl key_builder(std::move(config));

  std::unique_ptr<ScopeKey> key = key_builder.computeScopeKey(TestHeaderMapImpl{
      {"foo_header", "a=b,bar=bar_value,e=f"},
      {"bar_header", "a=b;bar=bar_value;index2"},
  });
  EXPECT_NE(key, nullptr);
  EXPECT_EQ(*key, makeKey({"bar_value", "index2"}));

  // Empty string fragment is fine.
  key = key_builder.computeScopeKey(TestHeaderMapImpl{
      {"foo_header", "a=b,bar,e=f"},
      {"bar_header", "a=b;bar=bar_value;"},
  });
  EXPECT_NE(key, nullptr);
  EXPECT_EQ(*key, makeKey({"", ""}));

  // Key not found.
  key = key_builder.computeScopeKey(TestHeaderMapImpl{
      {"foo_header", "a=b,meh,e=f"},
      {"bar_header", "a=b;bar=bar_value;"},
  });
  EXPECT_EQ(key, nullptr);

  // Index out of bound.
  key = key_builder.computeScopeKey(TestHeaderMapImpl{
      {"foo_header", "a=b,bar=bar_value,e=f"},
      {"bar_header", "a=b;bar=bar_value"},
  });
  EXPECT_EQ(key, nullptr);

  // Header missing.
  key = key_builder.computeScopeKey(TestHeaderMapImpl{
      {"foo_header", "a=b,bar=bar_value,e=f"},
      {"foobar_header", "a=b;bar=bar_value;index2"},
  });
  EXPECT_EQ(key, nullptr);

  // Header value empty.
  key = key_builder.computeScopeKey(TestHeaderMapImpl{
      {"foo_header", ""},
      {"bar_header", "a=b;bar=bar_value;index2"},
  });
  EXPECT_EQ(key, nullptr);

  // Case sensitive.
  key = key_builder.computeScopeKey(TestHeaderMapImpl{
      {"foo_header", "a=b,Bar=bar_value,e=f"},
      {"bar_header", "a=b;bar=bar_value;index2"},
  });
  EXPECT_EQ(key, nullptr);
}

class ScopedRouteInfoTest : public testing::Test {
public:
  void SetUp() override {
    std::string yaml_plain = R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: foo
        - string_key: bar
)EOF";
    TestUtility::loadFromYaml(yaml_plain, scoped_route_config_);

    route_config_ = std::make_shared<NiceMock<MockConfig>>();
    route_config_->name_ = "foo_route";
  }

  envoy::config::route::v3::RouteConfiguration route_configuration_;
  envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config_;
  std::shared_ptr<MockConfig> route_config_;
  std::unique_ptr<ScopedRouteInfo> info_;
};

TEST_F(ScopedRouteInfoTest, Creation) {
  envoy::config::route::v3::ScopedRouteConfiguration config_copy = scoped_route_config_;
  info_ = std::make_unique<ScopedRouteInfo>(std::move(scoped_route_config_), route_config_);
  EXPECT_EQ(info_->routeConfig().get(), route_config_.get());
  EXPECT_TRUE(TestUtility::protoEqual(info_->configProto(), config_copy));
  EXPECT_EQ(info_->scopeName(), "foo_scope");
  EXPECT_EQ(info_->scopeKey(), makeKey({"foo", "bar"}));
}

class ScopedConfigImplTest : public testing::Test {
public:
  void SetUp() override {
    std::string yaml_plain = R"EOF(
  fragments:
  - header_value_extractor:
      name: 'foo_header'
      element_separator: ','
      element:
        key: 'bar'
        separator: '='
  - header_value_extractor:
      name: 'bar_header'
      element_separator: ';'
      index: 2
)EOF";
    TestUtility::loadFromYaml(yaml_plain, key_builder_config_);

    scope_info_a_ = makeScopedRouteInfo(R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: foo
        - string_key: bar
)EOF");
    scope_info_a_v2_ = makeScopedRouteInfo(R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: xyz
        - string_key: xyz
)EOF");
    scope_info_b_ = makeScopedRouteInfo(R"EOF(
    name: bar_scope
    route_configuration_name: bar_route
    key:
      fragments:
        - string_key: bar
        - string_key: baz
)EOF");
  }
  std::shared_ptr<ScopedRouteInfo> makeScopedRouteInfo(const std::string& route_config_yaml) {
    envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config;
    TestUtility::loadFromYaml(route_config_yaml, scoped_route_config);

    std::shared_ptr<MockConfig> route_config = std::make_shared<NiceMock<MockConfig>>();
    route_config->name_ = scoped_route_config.route_configuration_name();
    return std::make_shared<ScopedRouteInfo>(std::move(scoped_route_config),
                                             std::move(route_config));
  }

  std::shared_ptr<ScopedRouteInfo> scope_info_a_;
  std::shared_ptr<ScopedRouteInfo> scope_info_a_v2_;
  std::shared_ptr<ScopedRouteInfo> scope_info_b_;
  ScopedRoutes::ScopeKeyBuilder key_builder_config_;
  std::unique_ptr<ScopedConfigImpl> scoped_config_impl_;
};

// Test a ScopedConfigImpl returns the correct route Config.
TEST_F(ScopedConfigImplTest, PickRoute) {
  scoped_config_impl_ = std::make_unique<ScopedConfigImpl>(std::move(key_builder_config_));
  scoped_config_impl_->addOrUpdateRoutingScope(scope_info_a_);
  scoped_config_impl_->addOrUpdateRoutingScope(scope_info_b_);

  // Key (foo, bar) maps to scope_info_a_.
  ConfigConstSharedPtr route_config = scoped_config_impl_->getRouteConfig(TestHeaderMapImpl{
      {"foo_header", ",,key=value,bar=foo,"},
      {"bar_header", ";val1;bar;val3"},
  });
  EXPECT_EQ(route_config, scope_info_a_->routeConfig());

  // Key (bar, baz) maps to scope_info_b_.
  route_config = scoped_config_impl_->getRouteConfig(TestHeaderMapImpl{
      {"foo_header", ",,key=value,bar=bar,"},
      {"bar_header", ";val1;baz;val3"},
  });
  EXPECT_EQ(route_config, scope_info_b_->routeConfig());

  // No such key (bar, NOT_BAZ).
  route_config = scoped_config_impl_->getRouteConfig(TestHeaderMapImpl{
      {"foo_header", ",key=value,bar=bar,"},
      {"bar_header", ";val1;NOT_BAZ;val3"},
  });
  EXPECT_EQ(route_config, nullptr);
}

// Test a ScopedConfigImpl returns the correct route Config before and after scope config update.
TEST_F(ScopedConfigImplTest, Update) {
  scoped_config_impl_ = std::make_unique<ScopedConfigImpl>(std::move(key_builder_config_));

  TestHeaderMapImpl headers{
      {"foo_header", ",,key=value,bar=foo,"},
      {"bar_header", ";val1;bar;val3"},
  };
  // Empty ScopeConfig.
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers), nullptr);

  // Add scope_key (bar, baz).
  scoped_config_impl_->addOrUpdateRoutingScope(scope_info_b_);
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers), nullptr);
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(
                TestHeaderMapImpl{{"foo_header", ",,key=v,bar=bar,"}, {"bar_header", ";val1;baz"}}),
            scope_info_b_->routeConfig());

  // Add scope_key (foo, bar).
  scoped_config_impl_->addOrUpdateRoutingScope(scope_info_a_);
  // Found scope_info_a_.
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers), scope_info_a_->routeConfig());

  // Update scope foo_scope.
  scoped_config_impl_->addOrUpdateRoutingScope(scope_info_a_v2_);
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers), nullptr);

  // foo_scope now is keyed by (xyz, xyz).
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(
                TestHeaderMapImpl{{"foo_header", ",bar=xyz,foo=bar"}, {"bar_header", ";;xyz"}}),
            scope_info_a_v2_->routeConfig());

  // Remove scope "foo_scope".
  scoped_config_impl_->removeRoutingScope("foo_scope");
  // scope_info_a_ is gone.
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers), nullptr);

  // Now delete some non-existent scopes.
  EXPECT_NO_THROW(scoped_config_impl_->removeRoutingScope("foo_scope1"));
  EXPECT_NO_THROW(scoped_config_impl_->removeRoutingScope("base_scope"));
  EXPECT_NO_THROW(scoped_config_impl_->removeRoutingScope("bluh_scope"));
  EXPECT_NO_THROW(scoped_config_impl_->removeRoutingScope("xyz_scope"));
}

} // namespace
} // namespace Router
} // namespace Envoy
