#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

// Integration test for ingestion of configuration across API versions.
// Currently we only have static tests, but there will also be xDS tests added
// later.
class VersionIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public HttpIntegrationTest {
public:
  VersionIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, VersionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Just IP tagging for now.
const char ExampleIpTaggingConfig[] = R"EOF(
    request_type: both
    ip_tags:
      - ip_tag_name: external_request
        ip_list:
          - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

// envoy.filters.http.ip_tagging from v2 Struct config.
TEST_P(VersionIntegrationTest, DEPRECATED_FEATURE_TEST(IpTaggingV2StaticStructConfig)) {
  config_helper_.addFilter(absl::StrCat(R"EOF(
  name: envoy.filters.http.ip_tagging
  config:
  )EOF",
                                        ExampleIpTaggingConfig));
  initialize();
}

// envoy.filters.http.ip_tagging from v2 TypedStruct config.
TEST_P(VersionIntegrationTest, IpTaggingV2StaticTypedStructConfig) {
  config_helper_.addFilter(absl::StrCat(R"EOF(
name: ip_tagging
typed_config:
  "@type": type.googleapis.com/udpa.type.v1.TypedStruct
  type_url: type.googleapis.com/envoy.config.filter.http.ip_tagging.v2.IPTagging
  value:
  )EOF",
                                        ExampleIpTaggingConfig));
  initialize();
}

// envoy.filters.http.ip_tagging from v3 TypedStruct config.
TEST_P(VersionIntegrationTest, IpTaggingV3StaticTypedStructConfig) {
  config_helper_.addFilter(absl::StrCat(R"EOF(
name: ip_tagging
typed_config:
  "@type": type.googleapis.com/udpa.type.v1.TypedStruct
  type_url: type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
  value:
  )EOF",
                                        ExampleIpTaggingConfig));
  initialize();
}

// envoy.filters.http.ip_tagging from v2 typed Any config.
TEST_P(VersionIntegrationTest, IpTaggingV2StaticTypedConfig) {
  config_helper_.addFilter(absl::StrCat(R"EOF(
  name: ip_tagging
  typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.ip_tagging.v2.IPTagging
  )EOF",
                                        ExampleIpTaggingConfig));
  initialize();
}

// envoy.filters.http.ip_tagging from v3 typed Any config.
TEST_P(VersionIntegrationTest, IpTaggingV3StaticTypedConfig) {
  config_helper_.addFilter(absl::StrCat(R"EOF(
  name: ip_tagging
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
  )EOF",
                                        ExampleIpTaggingConfig));
  initialize();
}

} // namespace
} // namespace Envoy
