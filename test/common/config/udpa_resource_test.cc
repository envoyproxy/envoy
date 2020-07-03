#include "common/config/udpa_resource.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

const std::string EscapedUri =
    "udpa://f123%25%2F%3F%23o/envoy.config.listener.v3.Listener/b%25%3A%2F%3F%23%5B%5Dar//"
    "baz?%25%23%5B%5D%26%3Dab=cde%25%23%5B%5D%26%3Df";
const std::string EscapedUriWithManyQueryParams =
    "udpa://f123%25%2F%3F%23o/envoy.config.listener.v3.Listener/b%25%3A%2F%3F%23%5B%5Dar//"
    "baz?%25%23%5B%5D%26%3D=bar&%25%23%5B%5D%26%3Dab=cde%25%23%5B%5D%26%3Df&foo=%25%23%5B%5D%26%3D";

// for all x. encodeUri(decodeUri(x)) = x where x comes from sample of valid udpa:// URIs.
// TODO(htuch): write a fuzzer that validates this property as well.
TEST(UdpaResourceNameTest, DecodeEncode) {
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
      EscapedUri,
      EscapedUriWithManyQueryParams,
  };
  UdpaResourceName::EncodeOptions encode_options;
  encode_options.sort_context_params_ = true;
  for (const std::string& uri : uris) {
    EXPECT_EQ(uri, UdpaResourceName::encodeUri(UdpaResourceName::decodeUri(uri), encode_options));
  }
}

// Validate that URI decoding behaves as expected component-wise.
TEST(UdpaResourceNameTest, DecodeSuccess) {
  const auto resource_name = UdpaResourceName::decodeUri(EscapedUriWithManyQueryParams);
  EXPECT_EQ("f123%/?#o", resource_name.authority());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_name.resource_type());
  EXPECT_EQ(3, resource_name.id().size());
  EXPECT_EQ("b%:/?#[]ar", resource_name.id()[0]);
  EXPECT_EQ("", resource_name.id()[1]);
  EXPECT_EQ("baz", resource_name.id()[2]);
  EXPECT_EQ(3, resource_name.context().params().size());
  EXPECT_EQ("bar", resource_name.context().params().at("%#[]&="));
  EXPECT_EQ("cde%#[]&=f", resource_name.context().params().at("%#[]&=ab"));
  EXPECT_EQ("%#[]&=", resource_name.context().params().at("foo"));
}

// Validate that the URI decoding behaves with a near-empty UDPA resource name.
TEST(UdpaResourceNameTest, DecodeEmpty) {
  const auto resource_name =
      UdpaResourceName::decodeUri("udpa:///envoy.config.listener.v3.Listener");
  EXPECT_TRUE(resource_name.authority().empty());
  EXPECT_EQ("envoy.config.listener.v3.Listener", resource_name.resource_type());
  EXPECT_TRUE(resource_name.id().empty());
  EXPECT_TRUE(resource_name.context().params().empty());
}

// Negative tests for URI decoding.
TEST(UdpaResourceNameTest, DecodeFail) {
  {
    EXPECT_THROW_WITH_MESSAGE(UdpaResourceName::decodeUri("foo://"),
                              UdpaResourceName::DecodeException,
                              "foo:// does not have an udpa scheme");
  }
  {
    EXPECT_THROW_WITH_MESSAGE(UdpaResourceName::decodeUri("udpa://foo"),
                              UdpaResourceName::DecodeException,
                              "Qualified type missing from udpa://foo");
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
