#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class JsonToMetadataIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  JsonToMetadataIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, JsonToMetadataIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Make sure that Envoy starts up with an ip tagging filter.
TEST_P(JsonToMetadataIntegrationTest, JsonToMetadataV3StaticTypedStructConfig) {
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.json_to_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.json_to_metadata.v3.JsonToMetadata
  request_rules:
    rules:
    - selectors:
      - key: version
      on_present:
        metadata_namespace: envoy.lb
        key: version
      on_missing:
        metadata_namespace: envoy.lb
        key: version
        value: 'unknown'
        preserve_existing_metadata_value: true
      on_error:
        metadata_namespace: envoy.lb
        key: version
        value: 'error'
        preserve_existing_metadata_value: true
)EOF");
  initialize();
}

} // namespace
} // namespace Envoy
