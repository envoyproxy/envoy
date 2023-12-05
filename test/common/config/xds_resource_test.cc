#include "source/common/config/xds_resource.h"

#include "test/common/config/xds_test_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::Pair;

namespace Envoy {
namespace Config {
namespace {

const std::string EscapedUrn =
    "xdstp://f123%25%2F%3F%23o/envoy.config.listener.v3.Listener/b%25%3A%3F%23%5B%5Dar//"
    "baz?%25%23%5B%5D%26%3Dab=cde%25%23%5B%5D%26%3Df";
const std::string EscapedUrnWithManyQueryParams =
    "xdstp://f123%25%2F%3F%23o/envoy.config.listener.v3.Listener/b%25%3A%3F%23%5B%5Dar//"
    "baz?%25%23%5B%5D%26%3D=bar&%25%23%5B%5D%26%3Dab=cde%25%23%5B%5D%26%3Df&foo=%25%23%5B%5D%26%3D";
const std::string EscapedUrlWithManyQueryParamsAndDirectives =
    EscapedUrnWithManyQueryParams +
    "#entry=some_en%25%23%5B%5D%2Ctry,alt=xdstp://fo%2525%252F%253F%2523o/bar%23alt=xdstp://bar/"
    "baz%2Centry=h%2525%2523%255B%255D%252Cuh";

// for all x. encodeUri(decodeUri(x)) = x where x comes from sample of valid xdstp:// URIs.
// TODO(htuch): write a fuzzer that validates this property as well.
TEST(XdsResourceIdentifierTest, DecodeEncode) {
  const std::vector<std::string> uris = {
      "xdstp:///envoy.config.listener.v3.Listener",
      "xdstp://foo/envoy.config.listener.v3.Listener",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar/baz",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar////baz",
      "xdstp://foo/envoy.config.listener.v3.Listener?ab=cde",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar?ab=cd",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar/baz?ab=cde",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar/baz?ab=",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar/baz?=cd",
      "xdstp://foo/envoy.config.listener.v3.Listener/bar/baz?ab=cde&ba=edc&z=f",
      EscapedUrn,
      EscapedUrnWithManyQueryParams,
  };
  XdsResourceIdentifier::EncodeOptions encode_options;
  encode_options.sort_context_params_ = true;
  for (const std::string& uri : uris) {
    EXPECT_EQ(uri, XdsResourceIdentifier::encodeUrn(XdsResourceIdentifier::decodeUrn(uri).value(),
                                                    encode_options));
    EXPECT_EQ(uri, XdsResourceIdentifier::encodeUrl(XdsResourceIdentifier::decodeUrl(uri),
                                                    encode_options));
  }
}

// Corner cases around path-identifier encoding/decoding.
TEST(XdsResourceIdentifierTest, PathDividerEscape) {
  {
    const auto resource_name =
        XdsResourceIdentifier::decodeUrn("xdstp:///type/foo%2Fbar/baz").value();
    EXPECT_EQ("foo/bar/baz", resource_name.id());
    EXPECT_EQ("xdstp:///type/foo/bar/baz", XdsResourceIdentifier::encodeUrn(resource_name));
  }
  {
    const auto resource_locator = XdsResourceIdentifier::decodeUrl("xdstp:///type/foo%2Fbar/baz");
    EXPECT_EQ("foo/bar/baz", resource_locator.id());
    EXPECT_EQ("xdstp:///type/foo/bar/baz", XdsResourceIdentifier::encodeUrl(resource_locator));
  }
}

// Validate that URN decoding behaves as expected component-wise.
TEST(XdsResourceNameTest, DecodeSuccess) {
  const auto resource_name =
      XdsResourceIdentifier::decodeUrn(EscapedUrnWithManyQueryParams).value();
  EXPECT_EQ("f123%/?#o", resource_name.authority());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_name.resource_type());
  EXPECT_EQ(resource_name.id(), "b%:?#[]ar//baz");
  EXPECT_CONTEXT_PARAMS(resource_name.context(), Pair("%#[]&=", "bar"),
                        Pair("%#[]&=ab", "cde%#[]&=f"), Pair("foo", "%#[]&="));
}

// Validate that URL decoding behaves as expected component-wise.
TEST(XdsResourceLocatorTest, DecodeSuccess) {
  const auto resource_locator =
      XdsResourceIdentifier::decodeUrl(EscapedUrlWithManyQueryParamsAndDirectives);
  EXPECT_EQ("f123%/?#o", resource_locator.authority());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_locator.resource_type());
  EXPECT_EQ(resource_locator.id(), "b%:?#[]ar//baz");
  EXPECT_CONTEXT_PARAMS(resource_locator.exact_context(), Pair("%#[]&=", "bar"),
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

// Validate that the URN decoding behaves with a near-empty xDS resource name.
TEST(XdsResourceLocatorTest, DecodeEmpty) {
  const auto resource_name =
      XdsResourceIdentifier::decodeUrn("xdstp:///envoy.config.listener.v3.Listener").value();
  EXPECT_TRUE(resource_name.authority().empty());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_name.resource_type());
  EXPECT_TRUE(resource_name.id().empty());
  EXPECT_TRUE(resource_name.context().params().empty());
}

// Validate that the URL decoding behaves with a near-empty xDS resource locator.
TEST(XdsResourceNameTest, DecodeEmpty) {
  const auto resource_locator =
      XdsResourceIdentifier::decodeUrl("xdstp:///envoy.config.listener.v3.Listener");
  EXPECT_TRUE(resource_locator.authority().empty());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_locator.resource_type());
  EXPECT_TRUE(resource_locator.id().empty());
  EXPECT_TRUE(resource_locator.exact_context().params().empty());
  EXPECT_TRUE(resource_locator.directives().empty());
}

// Negative tests for URN decoding.
TEST(XdsResourceNameTest, DecodeFail) {
  {
    EXPECT_EQ(XdsResourceIdentifier::decodeUrn("foo://").status().message(),
              "foo:// does not have an xdstp: scheme");
  }
  {
    EXPECT_EQ(XdsResourceIdentifier::decodeUrn("xdstp://foo").status().message(),
              "Resource type missing from /");
  }
}

// Negative tests for URL decoding.
TEST(XdsResourceLocatorTest, DecodeFail) {
  {
    EXPECT_THROW_WITH_MESSAGE(XdsResourceIdentifier::decodeUrl("foo://"), EnvoyException,
                              "foo:// does not have a xdstp:, http: or file: scheme");
  }
  {
    EXPECT_THROW_WITH_MESSAGE(XdsResourceIdentifier::decodeUrl("xdstp://foo"), EnvoyException,
                              "Resource type missing from /");
  }
  {
    EXPECT_THROW_WITH_MESSAGE(XdsResourceIdentifier::decodeUrl("xdstp://foo/some-type#bar=baz"),
                              EnvoyException, "Unknown fragment component bar=baz");
  }
}

// Validate parsing for xdstp:, http: and file: schemes.
TEST(XdsResourceLocatorTest, Schemes) {
  {
    const auto resource_locator =
        XdsResourceIdentifier::decodeUrl("xdstp://foo/bar/baz/blah?a=b#entry=m");
    EXPECT_EQ(xds::core::v3::ResourceLocator::XDSTP, resource_locator.scheme());
    EXPECT_EQ("foo", resource_locator.authority());
    EXPECT_EQ("bar", resource_locator.resource_type());
    EXPECT_EQ(resource_locator.id(), "baz/blah");
    EXPECT_CONTEXT_PARAMS(resource_locator.exact_context(), Pair("a", "b"));
    EXPECT_EQ(1, resource_locator.directives().size());
    EXPECT_EQ("m", resource_locator.directives()[0].entry());
    EXPECT_EQ("xdstp://foo/bar/baz/blah?a=b#entry=m",
              XdsResourceIdentifier::encodeUrl(resource_locator));
  }
  {
    const auto resource_locator =
        XdsResourceIdentifier::decodeUrl("http://foo/bar/baz/blah?a=b#entry=m");
    EXPECT_EQ(xds::core::v3::ResourceLocator::HTTP, resource_locator.scheme());
    EXPECT_EQ("foo", resource_locator.authority());
    EXPECT_EQ("bar", resource_locator.resource_type());
    EXPECT_EQ(resource_locator.id(), "baz/blah");
    EXPECT_CONTEXT_PARAMS(resource_locator.exact_context(), Pair("a", "b"));
    EXPECT_EQ(1, resource_locator.directives().size());
    EXPECT_EQ("m", resource_locator.directives()[0].entry());
    EXPECT_EQ("http://foo/bar/baz/blah?a=b#entry=m",
              XdsResourceIdentifier::encodeUrl(resource_locator));
  }
  {
    const auto resource_locator = XdsResourceIdentifier::decodeUrl("file:///bar/baz/blah#entry=m");
    EXPECT_EQ(xds::core::v3::ResourceLocator::FILE, resource_locator.scheme());
    EXPECT_EQ(resource_locator.id(), "bar/baz/blah");
    EXPECT_EQ(1, resource_locator.directives().size());
    EXPECT_EQ("m", resource_locator.directives()[0].entry());
    EXPECT_EQ("file:///bar/baz/blah#entry=m", XdsResourceIdentifier::encodeUrl(resource_locator));
  }
}

// extra tests for fragment handling

} // namespace
} // namespace Config
} // namespace Envoy
