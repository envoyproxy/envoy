#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class IpTaggingIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  IpTaggingIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IpTaggingIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Just IP tagging for now.
const char ExampleIpTaggingConfig[] = R"EOF(
    request_type: both
    ip_tags:
      - ip_tag_name: external_request
        ip_list:
          - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

// Make sure that Envoy starts up with an ip tagging filter.
TEST_P(IpTaggingIntegrationTest, IpTaggingV3StaticTypedStructConfig) {
  config_helper_.prependFilter(absl::StrCat(R"EOF(
name: ip_tagging
typed_config:
  "@type": type.googleapis.com/xds.type.v3.TypedStruct
  type_url: type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
  value:
  )EOF",
                                            ExampleIpTaggingConfig));
  initialize();
}

} // namespace
} // namespace Envoy
