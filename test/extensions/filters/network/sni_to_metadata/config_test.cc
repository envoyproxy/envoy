#include "source/extensions/filters/network/sni_to_metadata/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

// Test that the factory creates a filter config properly
TEST(SniToMetadataFilterConfigTest, ValidConfigProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniToMetadataFilterFactory factory;

  // Create a valid configuration
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;
  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$)");

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("policy_name");
  target->set_metadata_namespace("envoy.filters.network.sni_to_metadata");
  target->set_metadata_value("app-\\1");

  // Use the public interface to create filter factory
  auto cb_result = factory.createFilterFactoryFromProto(config, context);
  EXPECT_TRUE(cb_result.ok());
  Network::FilterFactoryCb cb = cb_result.value();
  EXPECT_TRUE(cb);

  // Test that the callback creates a filter when called
  NiceMock<Network::MockFilterManager> filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

// Test config validation - invalid regex should fail
TEST(SniToMetadataFilterConfigTest, InvalidRegexFails) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniToMetadataFilterFactory factory;

  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;
  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex("[invalid regex pattern"); // Missing closing bracket

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("test_key");
  target->set_metadata_namespace("test.namespace");
  target->set_metadata_value("\\1");

  // This should throw due to invalid regex
  EXPECT_THROW(auto result = factory.createFilterFactoryFromProto(config, context), EnvoyException);
}

// Test valid config with multiple rules and targets
TEST(SniToMetadataFilterConfigTest, ValidComplexConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniToMetadataFilterFactory factory;

  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  // First rule
  auto* rule1 = config.add_connection_rules();
  auto* pattern1 = rule1->mutable_pattern();
  pattern1->mutable_google_re2();
  pattern1->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$)");

  auto* target1 = rule1->add_metadata_targets();
  target1->set_metadata_key("app_name");
  target1->set_metadata_namespace("custom.metadata");
  target1->set_metadata_value("\\1");

  auto* target2 = rule1->add_metadata_targets();
  target2->set_metadata_key("region");
  target2->set_metadata_namespace("custom.metadata");
  target2->set_metadata_value("\\2");

  // Second rule
  auto* rule2 = config.add_connection_rules();
  auto* pattern2 = rule2->mutable_pattern();
  pattern2->mutable_google_re2();
  pattern2->set_regex(R"(^([^.]+)\.service\.([^.]+)\.com$)");

  auto* target3 = rule2->add_metadata_targets();
  target3->set_metadata_key("service_name");
  target3->set_metadata_namespace("another.namespace");
  target3->set_metadata_value("\\1");

  // This should succeed
  auto cb_result = factory.createFilterFactoryFromProto(config, context);
  EXPECT_TRUE(cb_result.ok());
  Network::FilterFactoryCb cb = cb_result.value();
  EXPECT_TRUE(cb);

  // Test that the callback creates a filter when called
  NiceMock<Network::MockFilterManager> filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

// Test default namespace behavior
TEST(SniToMetadataFilterConfigTest, DefaultNamespaceBehavior) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniToMetadataFilterFactory factory;

  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;
  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex(R"(^([^.]+)\..*$)");

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("test_key");
  // metadata_namespace is intentionally left empty to test default behavior
  target->set_metadata_value("\\1");

  // This should succeed - empty namespace should use default
  auto cb_result = factory.createFilterFactoryFromProto(config, context);
  EXPECT_TRUE(cb_result.ok());
  Network::FilterFactoryCb cb = cb_result.value();
  EXPECT_TRUE(cb);

  // Test that the callback creates a filter when called
  NiceMock<Network::MockFilterManager> filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

// Test valid config without pattern
TEST(SniToMetadataFilterConfigTest, ValidConfigWithoutPattern) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SniToMetadataFilterFactory factory;

  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;
  auto* rule = config.add_connection_rules();
  // No pattern specified - should always match

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("full_sni");
  target->set_metadata_namespace("test.namespace");
  // metadata_value empty - should use full SNI

  // This should succeed
  auto cb_result = factory.createFilterFactoryFromProto(config, context);
  EXPECT_TRUE(cb_result.ok());
  Network::FilterFactoryCb cb = cb_result.value();
  EXPECT_TRUE(cb);

  // Test that the callback creates a filter when called
  NiceMock<Network::MockFilterManager> filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy