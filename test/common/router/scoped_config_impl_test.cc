#include <memory>

#include "common/router/scoped_config_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::Http::TestHeaderMapImpl;

class FooFragment : public ScopeKeyFragmentBase {
private:
  virtual bool equals(const ScopeKeyFragmentBase&) const { return true; };
};

TEST(ScopeKeyFragmentBaseTest, EqualSign) {
  FooFragment foo;
  StringKeyFragment bar("a random string");

  EXPECT_FALSE(foo == bar);
}

TEST(StringKeyFragmentTest, Empty) {
  StringKeyFragment a("");
  StringKeyFragment b("");
  EXPECT_EQ(a, b);

  StringKeyFragment non_empty("ABC");

  EXPECT_FALSE(a == non_empty);
}

TEST(StringKeyFragmentTest, Normal) {
  StringKeyFragment str("Abc");

  StringKeyFragment same_str("Abc");
  EXPECT_EQ(str, same_str);

  StringKeyFragment upper_cased_str("ABC");
  EXPECT_FALSE(str == upper_cased_str);

  StringKeyFragment another_str("DEF");
  EXPECT_FALSE(str == another_str);
}

TEST(HeaderValueExtractorImplTest, InvalidConfig) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  // Type not set.
  EXPECT_DEATH(HeaderValueExtractorImpl{config}, "header_value_extractor is not set.");

  // Index non-zero when element separator is an empty string.
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
   index: 1
)EOF";
  TestUtility::loadFromYaml(yaml_plain, config);

  EXPECT_THROW_WITH_REGEX(HeaderValueExtractorImpl{config}, ProtoValidationException,
                          "when element separator is set to an empty string, index should be set "
                          "to 0 in HeaderValueExtractor.+");
  // extract_type not set.
  yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
)EOF";
  TestUtility::loadFromYaml(yaml_plain, config);

  EXPECT_THROW_WITH_REGEX(HeaderValueExtractorImpl{config}, ProtoValidationException,
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
  HeaderValueExtractorImpl extractor(config);
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
  HeaderValueExtractorImpl extractor(config);
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
  HeaderValueExtractorImpl extractor(config);
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
    if (part != nullptr) {
      key.addFragment(std::make_unique<StringKeyFragment>(part));
    } else {
      key.addFragment(nullptr);
    }
  }
  return key;
}

TEST(ScopeKeyTest, Unmatches) {
  ScopeKey key1;
  ScopeKey key2;
  // Empty key != empty key.
  EXPECT_NE(key1, key2);

  // Empty key != non-empty key.
  EXPECT_NE(key1, makeKey({""}));

  EXPECT_EQ(makeKey({"a", "b", "c"}), makeKey({"a", "b", "c"}));

  // A null part matches nothing.
  EXPECT_NE(makeKey({"a", "b", "c"}), makeKey({"a", "b", nullptr}));

  // A null part doesn't match "".
  EXPECT_NE(makeKey({"a", "b", ""}), makeKey({"a", "b", nullptr}));

  // Two keys of different length won't match.
  EXPECT_NE(makeKey({"a", "b"}), makeKey({"a", "b", "c"}));
  // Case sensitive.
  EXPECT_NE(makeKey({"a", "b"}), makeKey({"A", "b"}));
}

TEST(ScopeKeyTest, Matches) {
  // An empty string fragment equals another.
  EXPECT_EQ(makeKey({"", ""}), makeKey({"", ""}));
  EXPECT_EQ(makeKey({"a", "", ""}), makeKey({"a", "", ""}));

  // Non empty fragments  comparision.
  EXPECT_EQ(makeKey({"A", "b"}), makeKey({"A", "b"}));
}

} // namespace
} // namespace Router
} // namespace Envoy
