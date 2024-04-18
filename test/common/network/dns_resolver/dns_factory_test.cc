#include "source/common/network/address_impl.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"

#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Network {

class DnsFactoryTest : public testing::Test {
public:
  // Verify typed config is c-ares, and unpack to c-ares object.
  void verifyCaresDnsConfigAndUnpack(
      envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config,
      envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig& cares) {
    // Verify typed DNS resolver config is c-ares.
    EXPECT_EQ(typed_dns_resolver_config.name(), std::string(Network::CaresDnsResolver));
    EXPECT_EQ(typed_dns_resolver_config.typed_config().type_url(),
              "type.googleapis.com/"
              "envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig");
    typed_dns_resolver_config.typed_config().UnpackTo(&cares);
  }

  // Verify the c-ares object is default.
  void verifyCaresDnsConfigDefault(
      const envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig& cares) {
    EXPECT_EQ(false, cares.dns_resolver_options().use_tcp_for_dns_lookups());
    EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
    EXPECT_TRUE(cares.resolvers().empty());
  }
};

// Test default c-ares DNS resolver typed config creation is expected.
TEST_F(DnsFactoryTest, MakeDefaultCaresDnsResolverTest) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  makeDefaultCaresDnsResolverConfig(typed_dns_resolver_config);
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  verifyCaresDnsConfigDefault(cares);
}

// Test default apple DNS resolver typed config creation is expected.
TEST_F(DnsFactoryTest, MakeDefaultAppleDnsResolverTest) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  makeDefaultAppleDnsResolverConfig(typed_dns_resolver_config);
  EXPECT_EQ(typed_dns_resolver_config.name(), std::string(Network::AppleDnsResolver));
  EXPECT_EQ(
      typed_dns_resolver_config.typed_config().type_url(),
      "type.googleapis.com/envoy.extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig");
}

// Test default DNS resolver typed config creation based on build system and configuration is
// expected.
TEST_F(DnsFactoryTest, MakeDefaultDnsResolverTest) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  makeDefaultDnsResolverConfig(typed_dns_resolver_config);
  // In this test case, makeDefaultDnsResolverConfig() creates an default c-ares DNS typed config.
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  verifyCaresDnsConfigDefault(cares);
}

// Test handleLegacyDnsResolverData() function with DnsFilterConfig type.
TEST_F(DnsFactoryTest, LegacyDnsResolverDataDnsFilterConfig) {
  envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig::ClientContextConfig
      dns_filter_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  handleLegacyDnsResolverData(dns_filter_config, typed_dns_resolver_config);
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  verifyCaresDnsConfigDefault(cares);
}

// Test handleLegacyDnsResolverData() function with Cluster type, and default config.
TEST_F(DnsFactoryTest, LegacyDnsResolverDataClusterConfigDefault) {
  envoy::config::cluster::v3::Cluster cluster_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  handleLegacyDnsResolverData(cluster_config, typed_dns_resolver_config);
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  verifyCaresDnsConfigDefault(cares);
}

// Test handleLegacyDnsResolverData() function with Cluster type, and non-default config.
TEST_F(DnsFactoryTest, LegacyDnsResolverDataClusterConfigNonDefault) {
  envoy::config::cluster::v3::Cluster cluster_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cluster_config.set_use_tcp_for_dns_lookups(true);
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 8080),
                                             resolvers);
  cluster_config.add_dns_resolvers()->MergeFrom(resolvers);
  handleLegacyDnsResolverData(cluster_config, typed_dns_resolver_config);
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_FALSE(cares.resolvers().empty());
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(0), resolvers));
}

// Test handleLegacyDnsResolverData() function with Bootstrap type, and non-default config.
TEST_F(DnsFactoryTest, LegacyDnsResolverDataBootstrapConfigNonDefault) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  bootstrap_config.set_use_tcp_for_dns_lookups(true);
  handleLegacyDnsResolverData(bootstrap_config, typed_dns_resolver_config);
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_TRUE(cares.resolvers().empty());
}

// Test handleLegacyDnsResolverData() function with DnsCacheConfig type, and non-default config.
TEST_F(DnsFactoryTest, LegacyDnsResolverDataDnsCacheConfigNonDefault) {
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig dns_cache_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  dns_cache_config.set_use_tcp_for_dns_lookups(true);
  handleLegacyDnsResolverData(dns_cache_config, typed_dns_resolver_config);
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_TRUE(cares.resolvers().empty());
}

// Test checkDnsResolutionConfigExist() function with Bootstrap type,
// and dns_resolution_config exists.
TEST_F(DnsFactoryTest, CheckDnsResolutionConfigExistWithBoostrap) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  bootstrap_config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_use_tcp_for_dns_lookups(true);
  bootstrap_config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_no_default_search_domain(true);

  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 8080),
                                             resolvers);
  bootstrap_config.mutable_dns_resolution_config()->add_resolvers()->MergeFrom(resolvers);
  EXPECT_TRUE(checkDnsResolutionConfigExist(bootstrap_config, typed_dns_resolver_config));
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(true, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_FALSE(cares.resolvers().empty());
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(0), resolvers));
}

// Test checkTypedDnsResolverConfigExist() function with Bootstrap type,
// and typed_dns_resolver_config exists.
TEST_F(DnsFactoryTest, CheckTypedDnsResolverConfigExistWithBoostrap) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;

  cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(true);
  cares.mutable_dns_resolver_options()->set_no_default_search_domain(true);
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 8080),
                                             resolvers);
  cares.add_resolvers()->MergeFrom(resolvers);
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  bootstrap_config.mutable_typed_dns_resolver_config()->MergeFrom(typed_dns_resolver_config);
  EXPECT_TRUE(bootstrap_config.has_typed_dns_resolver_config());
  typed_dns_resolver_config.Clear();
  cares.Clear();

  EXPECT_TRUE(checkTypedDnsResolverConfigExist(bootstrap_config, typed_dns_resolver_config));
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(true, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_FALSE(cares.resolvers().empty());
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(0), resolvers));
}

// Test checkTypedDnsResolverConfigExist() function with Bootstrap type.
// A garbage typed_dns_resolver_config type foo exists with dns_resolution_config.
// In this case, the typed_dns_resolver_config is copied over.
TEST_F(DnsFactoryTest, CheckBothTypedAndDnsResolutionConfigExistWithBoostrapWrongType) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap_config;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;

  typed_dns_resolver_config.mutable_typed_config()->set_type_url("type.googleapis.com/foo");
  typed_dns_resolver_config.mutable_typed_config()->set_value("bar");
  typed_dns_resolver_config.set_name("baz");
  bootstrap_config.mutable_typed_dns_resolver_config()->MergeFrom(typed_dns_resolver_config);
  EXPECT_TRUE(bootstrap_config.has_typed_dns_resolver_config());
  typed_dns_resolver_config.Clear();

  // setup dns_resolution_config with multiple resolver addresses
  envoy::config::core::v3::Address resolvers0;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 8080),
                                             resolvers0);
  bootstrap_config.mutable_dns_resolution_config()->add_resolvers()->MergeFrom(resolvers0);
  envoy::config::core::v3::Address resolvers1;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("5.6.7.8", 8081),
                                             resolvers1);
  bootstrap_config.mutable_dns_resolution_config()->add_resolvers()->MergeFrom(resolvers1);
  bootstrap_config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_use_tcp_for_dns_lookups(true);
  bootstrap_config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_no_default_search_domain(true);

  // setup use_tcp_for_dns_lookups
  bootstrap_config.set_use_tcp_for_dns_lookups(false);

  EXPECT_TRUE(checkTypedDnsResolverConfigExist(bootstrap_config, typed_dns_resolver_config));
  EXPECT_FALSE(tryUseAppleApiForDnsLookups(typed_dns_resolver_config));
  typed_dns_resolver_config = makeDnsResolverConfig(bootstrap_config);

  // verify the typed_dns_resolver_config data matching DNS resolution config
  EXPECT_EQ(typed_dns_resolver_config.name(), "baz");
  EXPECT_EQ(typed_dns_resolver_config.typed_config().type_url(), "type.googleapis.com/foo");
  EXPECT_EQ(typed_dns_resolver_config.typed_config().value(), "bar");
}

// Test default DNS resolver factory creation based on build system and configuration is
// expected.
TEST_F(DnsFactoryTest, MakeDefaultDnsResolverFactoryTestInCares) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  Network::DnsResolverFactory& dns_resolver_factory =
      Envoy::Network::createDefaultDnsResolverFactory(typed_dns_resolver_config);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  verifyCaresDnsConfigDefault(cares);
  EXPECT_EQ(dns_resolver_factory.name(), std::string(CaresDnsResolver));
}

// Test DNS resolver factory creation from proto without typed config.
TEST_F(DnsFactoryTest, MakeDnsResolverFactoryFromProtoTestInCaresWithoutTypedConfig) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  Network::DnsResolverFactory& dns_resolver_factory =
      Envoy::Network::createDnsResolverFactoryFromProto(envoy::config::bootstrap::v3::Bootstrap(),
                                                        typed_dns_resolver_config);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  verifyCaresDnsConfigDefault(cares);
  EXPECT_EQ(dns_resolver_factory.name(), std::string(CaresDnsResolver));
}

// Test DNS resolver factory creation from proto with valid typed config
TEST_F(DnsFactoryTest, MakeDnsResolverFactoryFromProtoTestInCaresWithGoodTypedConfig) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;

  typed_dns_resolver_config.mutable_typed_config()->set_type_url(
      "type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig");
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  config.mutable_typed_dns_resolver_config()->MergeFrom(typed_dns_resolver_config);
  Network::DnsResolverFactory& dns_resolver_factory =
      Envoy::Network::createDnsResolverFactoryFromProto(config, typed_dns_resolver_config);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  verifyCaresDnsConfigDefault(cares);
  EXPECT_EQ(dns_resolver_factory.name(), std::string(CaresDnsResolver));
}

// Test DNS resolver factory creation from proto with invalid typed config
TEST_F(DnsFactoryTest, MakeDnsResolverFactoryFromProtoTestInCaresWithInvalidTypedConfig) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;

  typed_dns_resolver_config.mutable_typed_config()->set_type_url("type.googleapis.com/foo");
  typed_dns_resolver_config.set_name("bar");
  config.mutable_typed_dns_resolver_config()->MergeFrom(typed_dns_resolver_config);
  EXPECT_THROW_WITH_MESSAGE(
      Envoy::Network::createDnsResolverFactoryFromProto(config, typed_dns_resolver_config),
      Envoy::EnvoyException,
      "Didn't find a registered implementation for 'bar' with type URL: 'foo'");
}

} // namespace Network
} // namespace Envoy
