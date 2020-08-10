#include "common/config/udpa_resource.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

#define EXPECT_CONTEXT_PARAMS(context_params, ...)                                                 \
  {                                                                                                \
    std::map<std::string, std::string> param_map((context_params).begin(),                         \
                                                 (context_params).end());                          \
    EXPECT_THAT(param_map, UnorderedElementsAre(__VA_ARGS__));                                     \
  }

namespace Envoy {
namespace Config {
namespace {

const std::string EscapedUrn =
    "udpa://f123%25%2F%3F%23o/envoy.config.listener.v3.Listener/b%25%3A%2F%3F%23%5B%5Dar//"
    "baz?%25%23%5B%5D%26%3Dab=cde%25%23%5B%5D%26%3Df";
const std::string EscapedUrnWithManyQueryParams =
    "udpa://f123%25%2F%3F%23o/envoy.config.listener.v3.Listener/b%25%3A%2F%3F%23%5B%5Dar//"
    "baz?%25%23%5B%5D%26%3D=bar&%25%23%5B%5D%26%3Dab=cde%25%23%5B%5D%26%3Df&foo=%25%23%5B%5D%26%3D";
const std::string EscapedUrlWithManyQueryParamsAndDirectives =
    EscapedUrnWithManyQueryParams +
    "#entry=some_en%25%23%5B%5D%2Ctry,alt=udpa://fo%2525%252F%253F%2523o/bar%23alt=udpa://bar/"
    "baz%2Centry=h%2525%2523%255B%255D%252Cuh";

// for all x. encodeUri(decodeUri(x)) = x where x comes from sample of valid udpa:// URIs.
// TODO(htuch): write a fuzzer that validates this property as well.
TEST(UdpaResourceIdentifierTest, DecodeEncode) {
  const std::vector<std::string> uris = {
      "udpa:///envoy.config.listener.v3.Listener",
      "udpa://foo/envoy.config.listener.v3.Listener",
      "udpa://foo/envoy.config.listener.v3.Listener/bar",
      "udpa://foo/envoy.config.listener.v3.Listener/bar/baz",
      "udpa://foo/envoy.config.listener.v3.Listener/bar////baz",
      "udpa://foo/envoy.config.listener.v3.Listener?ab=cde",
      "udpa://foo/envoy.config.listener.v3.Listener/bar?ab=cd",
      "udpa://foo/envoy.config.listener.v3.Listener/bar/baz?ab=cde",
      "udpa://foo/envoy.config.listener.v3.Listener/bar/baz?ab=",
      "udpa://foo/envoy.config.listener.v3.Listener/bar/baz?=cd",
      "udpa://foo/envoy.config.listener.v3.Listener/bar/baz?ab=cde&ba=edc&z=f",
      EscapedUrn,
      EscapedUrnWithManyQueryParams,
  };
  UdpaResourceIdentifier::EncodeOptions encode_options;
  encode_options.sort_context_params_ = true;
  for (const std::string& uri : uris) {
    EXPECT_EQ(uri, UdpaResourceIdentifier::encodeUrn(UdpaResourceIdentifier::decodeUrn(uri),
                                                     encode_options));
    EXPECT_EQ(uri, UdpaResourceIdentifier::encodeUrl(UdpaResourceIdentifier::decodeUrl(uri),
                                                     encode_options));
  }
}

// Validate that URN decoding behaves as expected component-wise.
TEST(UdpaResourceNameTest, DecodeSuccess) {
  const auto resource_name = UdpaResourceIdentifier::decodeUrn(EscapedUrnWithManyQueryParams);
  EXPECT_EQ("f123%/?#o", resource_name.authority());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_name.resource_type());
  EXPECT_THAT(resource_name.id(), ElementsAre("b%:/?#[]ar", "", "baz"));
  EXPECT_CONTEXT_PARAMS(resource_name.context().params(), Pair("%#[]&=", "bar"),
                        Pair("%#[]&=ab", "cde%#[]&=f"), Pair("foo", "%#[]&="));
}

// Validate that URL decoding behaves as expected component-wise.
TEST(UdpaResourceLocatorTest, DecodeSuccess) {
  const auto resource_locator =
      UdpaResourceIdentifier::decodeUrl(EscapedUrlWithManyQueryParamsAndDirectives);
  EXPECT_EQ("f123%/?#o", resource_locator.authority());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_locator.resource_type());
  EXPECT_THAT(resource_locator.id(), ElementsAre("b%:/?#[]ar", "", "baz"));
  EXPECT_CONTEXT_PARAMS(resource_locator.exact_context().params(), Pair("%#[]&=", "bar"),
                        Pair("%#[]&=ab", "cde%#[]&=f"), Pair("foo", "%#[]&="));
  EXPECT_EQ(2, resource_locator.directives().size());
  EXPECT_EQ("some_en%#[],try", resource_locator.directives()[0].entry());
  const auto& alt = resource_locator.directives()[1].alt();
  EXPECT_EQ("fo%/?#o", alt.authority());
  EXPECT_EQ("bar", alt.resource_type());
  EXPECT_EQ(2, alt.directives().size());
  const auto& inner_alt = alt.directives()[0].alt();
  EXPECT_EQ("bar", inner_alt.authority());
  EXPECT_EQ("baz", inner_alt.resource_type());
  EXPECT_EQ("h%#[],uh", alt.directives()[1].entry());
}

// Validate that the URN decoding behaves with a near-empty UDPA resource name.
TEST(UdpaResourceLocatorTest, DecodeEmpty) {
  const auto resource_name =
      UdpaResourceIdentifier::decodeUrn("udpa:///envoy.config.listener.v3.Listener");
  EXPECT_TRUE(resource_name.authority().empty());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_name.resource_type());
  EXPECT_TRUE(resource_name.id().empty());
  EXPECT_TRUE(resource_name.context().params().empty());
}

// Validate that the URL decoding behaves with a near-empty UDPA resource locator.
TEST(UdpaResourceNameTest, DecodeEmpty) {
  const auto resource_locator =
      UdpaResourceIdentifier::decodeUrl("udpa:///envoy.config.listener.v3.Listener");
  EXPECT_TRUE(resource_locator.authority().empty());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_locator.resource_type());
  EXPECT_TRUE(resource_locator.id().empty());
  EXPECT_TRUE(resource_locator.exact_context().params().empty());
  EXPECT_TRUE(resource_locator.directives().empty());
}

// Negative tests for URN decoding.
TEST(UdpaResourceNameTest, DecodeFail) {
  {
    EXPECT_THROW_WITH_MESSAGE(UdpaResourceIdentifier::decodeUrn("foo://"),
                              UdpaResourceIdentifier::DecodeException,
                              "foo:// does not have an udpa: scheme");
  }
  {
    EXPECT_THROW_WITH_MESSAGE(UdpaResourceIdentifier::decodeUrn("udpa://foo"),
                              UdpaResourceIdentifier::DecodeException,
                              "Resource type missing from /");
  }
}

// Negative tests for URL decoding.
TEST(UdpaResourceLocatorTest, DecodeFail) {
  {
    EXPECT_THROW_WITH_MESSAGE(UdpaResourceIdentifier::decodeUrl("foo://"),
                              UdpaResourceIdentifier::DecodeException,
                              "foo:// does not have a udpa:, http: or file: scheme");
  }
  {
    EXPECT_THROW_WITH_MESSAGE(UdpaResourceIdentifier::decodeUrl("udpa://foo"),
                              UdpaResourceIdentifier::DecodeException,
                              "Resource type missing from /");
  }
  {
    EXPECT_THROW_WITH_MESSAGE(UdpaResourceIdentifier::decodeUrl("udpa://foo/some-type#bar=baz"),
                              UdpaResourceIdentifier::DecodeException,
                              "Unknown fragment component bar=baz");
  }
}

// Validate parsing for udpa:, http: and file: schemes.
TEST(UdpaResourceLocatorTest, Schemes) {
  {
    const auto resource_locator =
        UdpaResourceIdentifier::decodeUrl("udpa://foo/bar/baz/blah?a=b#entry=m");
    EXPECT_EQ(udpa::core::v1::ResourceLocator::UDPA, resource_locator.scheme());
    EXPECT_EQ("foo", resource_locator.authority());
    EXPECT_EQ("bar", resource_locator.resource_type());
    EXPECT_THAT(resource_locator.id(), ElementsAre("baz", "blah"));
    EXPECT_CONTEXT_PARAMS(resource_locator.exact_context().params(), Pair("a", "b"));
    EXPECT_EQ(1, resource_locator.directives().size());
    EXPECT_EQ("m", resource_locator.directives()[0].entry());
    EXPECT_EQ("udpa://foo/bar/baz/blah?a=b#entry=m",
              UdpaResourceIdentifier::encodeUrl(resource_locator));
  }
  {
    const auto resource_locator =
        UdpaResourceIdentifier::decodeUrl("http://foo/bar/baz/blah?a=b#entry=m");
    EXPECT_EQ(udpa::core::v1::ResourceLocator::HTTP, resource_locator.scheme());
    EXPECT_EQ("foo", resource_locator.authority());
    EXPECT_EQ("bar", resource_locator.resource_type());
    EXPECT_THAT(resource_locator.id(), ElementsAre("baz", "blah"));
    EXPECT_CONTEXT_PARAMS(resource_locator.exact_context().params(), Pair("a", "b"));
    EXPECT_EQ(1, resource_locator.directives().size());
    EXPECT_EQ("m", resource_locator.directives()[0].entry());
    EXPECT_EQ("http://foo/bar/baz/blah?a=b#entry=m",
              UdpaResourceIdentifier::encodeUrl(resource_locator));
  }
  {
    const auto resource_locator = UdpaResourceIdentifier::decodeUrl("file:///bar/baz/blah#entry=m");
    EXPECT_EQ(udpa::core::v1::ResourceLocator::FILE, resource_locator.scheme());
    EXPECT_THAT(resource_locator.id(), ElementsAre("bar", "baz", "blah"));
    EXPECT_EQ(1, resource_locator.directives().size());
    EXPECT_EQ("m", resource_locator.directives()[0].entry());
    EXPECT_EQ("file:///bar/baz/blah#entry=m", UdpaResourceIdentifier::encodeUrl(resource_locator));
  }
}

// extra tests for fragment handling

} // namespace
} // namespace Config
} // namespace Envoy
