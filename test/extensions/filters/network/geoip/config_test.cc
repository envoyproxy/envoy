#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"
#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.validate.h"

#include "source/extensions/filters/network/geoip/config.h"

#include "test/extensions/filters/http/geoip/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Import the shared geoip mocks from the HTTP filter tests.
using Envoy::Extensions::HttpFilters::Geoip::DummyGeoipProviderFactory;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {
namespace {

class GeoipConfigTest : public testing::Test {
public:
  void initializeProviderFactory() { registration_.emplace(dummy_factory_); }

  DummyGeoipProviderFactory dummy_factory_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  absl::optional<Registry::InjectFactory<Geolocation::GeoipProviderFactory>> registration_;
};

TEST_F(GeoipConfigTest, CreateFilterFactory) {
  initializeProviderFactory();
  const std::string config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";

  envoy::extensions::filters::network::geoip::v3::Geoip proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);

  GeoipFilterFactory factory;
  auto status_or_cb = factory.createFilterFactoryFromProto(proto_config, context_);
  ASSERT_TRUE(status_or_cb.ok());
  Network::FilterFactoryCb cb = status_or_cb.value();
  EXPECT_NE(nullptr, cb);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST_F(GeoipConfigTest, InvalidConfigMissingProvider) {
  envoy::extensions::filters::network::geoip::v3::Geoip proto_config;
  // Proto validation fails for missing required provider field.
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate("{}", proto_config), EnvoyException,
                          "Provider: value is required");
}

TEST_F(GeoipConfigTest, FilterIsNotTerminal) {
  initializeProviderFactory();
  const std::string config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";

  GeoipFilterFactory factory;
  envoy::extensions::filters::network::geoip::v3::Geoip proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  EXPECT_FALSE(factory.isTerminalFilterByProto(proto_config, context_.serverFactoryContext()));
}

} // namespace
} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
