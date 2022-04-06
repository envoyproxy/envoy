#include <memory>

#include "source/extensions/common/matcher/domain_matcher.h"

#include "test/extensions/common/matcher/custom_matcher_test.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {
namespace {

using ::Envoy::Matcher::TestDataInputFactory;

class DomainMatcherTest
    : public CustomMatcherTest<DomainMatcherFactoryBase<::Envoy::Matcher::TestData>> {};

TEST_F(DomainMatcherTest, TestMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - domains:
        - example.com
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: bar
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputFactory("input", "example.com");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory("input", "envoy.com");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputFactory("input", "envoy.org");
    validateNoMatch();
  }
  {
    auto input = TestDataInputFactory("input", "xxx");
    validateNoMatch();
  }
}

TEST_F(DomainMatcherTest, TestMatcherInvalidWildcard) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "com.*"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
  )EOF";
  loadConfig(yaml);
  auto input = TestDataInputFactory("input", "example.com");
  EXPECT_THROW_WITH_MESSAGE(validateMatch("foo"), EnvoyException, "invalid domain wildcard: com.*");
}

TEST_F(DomainMatcherTest, TestMatcherUnicodeDomain) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "®.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
  )EOF";
  loadConfig(yaml);
  auto input = TestDataInputFactory("input", "example.com");
  EXPECT_THROW_WITH_MESSAGE(validateMatch("foo"), EnvoyException,
                            "non-ASCII domains are not supported: ®.com");
}

TEST_F(DomainMatcherTest, TestMatcherDuplicateDomain) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - domains:
        - "EXAMPLE.COM"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: bar
  )EOF";
  loadConfig(yaml);
  auto input = TestDataInputFactory("input", "example.com");
  EXPECT_THROW_WITH_MESSAGE(validateMatch("foo"), EnvoyException, "duplicate domain: EXAMPLE.COM");
}

TEST_F(DomainMatcherTest, TestMatcherOnNoMatch) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
on_no_match:
  action:
    name: bar
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: bar
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputFactory("input", "example.com");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputFactory("input", "envoy.com");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory("input", "xxx");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory(
        "input", {DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt});
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory(
        "input", {DataInputGetResult::DataAvailability::MoreDataMightBeAvailable, "example.com"});
    validateUnableToMatch();
  }
}

TEST_F(DomainMatcherTest, TestMatcherComplex) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: m1
      - domains:
        - "*.com"
        - "*.org"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: m2
      - domains:
        - "com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: m3
      - domains:
        - "example.com"
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: m4
      - domains:
        - "sub.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: m5
      - domains:
        - "*.envoy.org"
        on_match:
          matcher:
            matcher_tree:
              input:
                name: input
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
              custom_match:
                name: matcher
                typed_config:
                  "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
                  domain_matchers:
                  - domains:
                    - "*.sub.envoy.org"
                    on_match:
                      action:
                        name: test_action
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.StringValue
                          value: m6
                  - domains:
                    - "*"
                    on_match:
                      action:
                        name: test_action
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.StringValue
                          value: m7
  )EOF";
  loadConfig(yaml);

  // The trie above represents the following decision tree:
  //
  //  *(m1 prefix)
  //  com(m2 prefix, m3 exact)           org(m2 prefix)
  //  example.com (m4)
  //  sub.example.com (m5 exact)         envoy.org (m7 prefix)
  //                                     sub.envoy.org (m6 prefix)
  {
    auto input = TestDataInputFactory("input", "envoy");
    validateMatch("m1");
  }
  {
    auto input = TestDataInputFactory("input", "com");
    validateMatch("m3");
  }
  {
    auto input = TestDataInputFactory("input", "org");
    validateMatch("m1");
  }
  {
    auto input = TestDataInputFactory("input", "envoy.com");
    validateMatch("m2");
  }
  {
    auto input = TestDataInputFactory("input", "example.com");
    validateMatch("m4");
  }
  {
    auto input = TestDataInputFactory("input", "host.example.com");
    validateMatch("m4");
  }
  {
    auto input = TestDataInputFactory("input", "sub.example.com");
    validateMatch("m5");
  }
  {
    auto input = TestDataInputFactory("input", "host.sub.example.com");
    validateMatch("m4");
  }
  {
    auto input = TestDataInputFactory("input", "envoy.org");
    validateMatch("m2");
  }
  {
    auto input = TestDataInputFactory("input", "sub.envoy.org");
    validateMatch("m7");
  }
  {
    auto input = TestDataInputFactory("input", "host.sub.envoy.org");
    validateMatch("m6");
  }
}

} // namespace
} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
