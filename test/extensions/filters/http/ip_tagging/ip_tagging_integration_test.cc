#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

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
const std::string ExampleIpTaggingConfig = R"EOF(
  name: ip_tagging
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
    request_type: both
    ip_tags:
      - ip_tag_name: external_request
        ip_list:
          - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

const std::string FileBasedIpTaggingConfig = R"EOF(
  name: ip_tagging
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
    request_type: both
    ip_tags_path: "{{ test_rundir }}/test/extensions/filters/http/ip_tagging/test_data/ip_tags_external_request.yaml"
)EOF";

// Make sure that Envoy starts up with an ip tagging filter.
TEST_P(IpTaggingIntegrationTest, IpTaggingV3StaticTypedStructConfig) {
  config_helper_.prependFilter(ExampleIpTaggingConfig);
  initialize();
}

TEST_P(IpTaggingIntegrationTest, FileBasedIpTagging) {
  config_helper_.prependFilter(TestEnvironment::substitute(FileBasedIpTaggingConfig));
  initialize();
}

} // namespace
} // namespace Envoy
