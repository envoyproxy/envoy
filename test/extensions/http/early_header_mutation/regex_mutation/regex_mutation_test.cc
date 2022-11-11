#include "source/extensions/http/early_header_mutation/regex_mutation/regex_mutation.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace RegexMutation {
namespace {

using ProtoRegexMutation =
    envoy::extensions::http::early_header_mutation::regex_mutation::v3::RegexMutation;

TEST(RegexMutationTest, Basic) {
  ScopedInjectableLoader<Regex::Engine> engine{std::make_unique<Regex::GoogleReEngine>()};

  const std::string config = R"EOF(
  header_mutations:
    - header: "foo"
      rename: "bar"
      regex_rewrite:
        pattern:
          regex: "foo"
        substitution: "bar"
    - header: "baz"
      regex_rewrite:
        pattern:
          regex: "^baz$"
        substitution: "qux"
    - header: "sub-group"
      regex_rewrite:
        pattern:
          regex: "^(abcd)(efg)$"
        substitution: "\\2O_O\\1"
    - header: ":method"
      rename: "add-header"
      regex_rewrite:
        pattern:
          regex: ".*"
        substitution: "add-header-value"
  )EOF";

  ProtoRegexMutation proto_mutation;
  TestUtility::loadFromYaml(config, proto_mutation);

  RegexMutation mutation(proto_mutation.header_mutations());

  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {":method", "GET"},
    };

    EXPECT_TRUE(mutation.mutate(headers));

    // Other header mutations are not applied because the header is not present.
    EXPECT_EQ(2, headers.size());
    EXPECT_EQ("add-header-value", headers.get_("add-header"));
  }

  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"foo", "foofoofoo"},
        {"baz", "bazbazbaz"},
        {"sub-group", "abcdefg"},
        {":method", "GET"},
    };

    EXPECT_TRUE(mutation.mutate(headers));

    // Rename will add a new header but doesn't remove the old one.
    EXPECT_EQ("foofoofoo", headers.get_("foo"));
    EXPECT_EQ("bazbazbaz", headers.get_("baz"));

    // Header value not match the regex pattern and do nothing for this header.
    EXPECT_EQ("bazbazbaz", headers.get_("baz"));

    // Header value match the regex pattern and rewrite the header value.
    EXPECT_EQ("efgO_Oabcd", headers.get_("sub-group"));

    EXPECT_EQ("add-header-value", headers.get_("add-header"));
  }
}

TEST(RegexMutationTest, MutateSameHeader) {
  ScopedInjectableLoader<Regex::Engine> engine{std::make_unique<Regex::GoogleReEngine>()};

  const std::string config = R"EOF(
  header_mutations:
    - header: "foo"
      regex_rewrite:
        pattern:
          regex: "foo"
        substitution: "bar"
    - header: "foo"
      regex_rewrite:
        pattern:
          regex: "bar"
        substitution: "qux"
    - header: "foo"
      rename: "bar"
      regex_rewrite:
        pattern:
          regex: "^(.*)$"
        substitution: "\\1"
  )EOF";

  ProtoRegexMutation proto_mutation;
  TestUtility::loadFromYaml(config, proto_mutation);

  RegexMutation mutation(proto_mutation.header_mutations());

  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"foo", "foofoofoo"},
    };

    EXPECT_TRUE(mutation.mutate(headers));

    // The first mutation rule update the 'foo' header value to 'barbarbar' and the second
    // mutation rule update the 'foo' header value to 'quxquxqux'.
    EXPECT_EQ("quxquxqux", headers.get_("foo"));

    // The third mutation rule add new header 'bar' and copy the value from 'foo'.
    EXPECT_EQ("quxquxqux", headers.get_("bar"));
  }
}

} // namespace
} // namespace RegexMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
