#include "source/extensions/filters/network/sni_to_metadata/filter.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

class SniToMetadataFilterTest : public testing::Test {
public:
  void setUpFilter(
      const envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter& config) {
    auto config_shared =
        std::make_shared<Config>(config, context_.serverFactoryContext().regexEngine());
    filter_ = std::make_unique<Filter>(config_shared);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);

    ON_CALL(filter_callbacks_, connection()).WillByDefault(ReturnRef(connection_));
    ON_CALL(connection_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
    ON_CALL(Const(connection_), streamInfo()).WillByDefault(ReturnRef(stream_info_));
    ON_CALL(stream_info_, dynamicMetadata()).WillByDefault(ReturnRef(dynamic_metadata_));
  }

  // Helper to create a basic config with a single rule
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter createBasicConfig() {
    envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

    auto* rule = config.add_connection_rules();
    auto* pattern = rule->mutable_pattern();
    pattern->mutable_google_re2();
    pattern->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$)");

    auto* target = rule->add_metadata_targets();
    target->set_metadata_key("shard_id");
    target->set_metadata_namespace("envoy.filters.network.sni_to_metadata");
    target->set_metadata_value("shard-\\1-\\2");

    return config;
  }

protected:
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
};

// Test successful SNI extraction and metadata setting with capture groups
TEST_F(SniToMetadataFilterTest, SuccessfulSniExtractionWithCaptureGroups) {
  auto config = createBasicConfig();
  setUpFilter(config);

  // Mock SNI from requestedServerName
  ON_CALL(connection_, requestedServerName())
      .WillByDefault(Return("myapp.us-west-2.prod.example.com"));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify the metadata was set correctly with capture group substitution
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("envoy.filters.network.sni_to_metadata"));

  const auto& metadata = filter_metadata.at("envoy.filters.network.sni_to_metadata");
  ASSERT_TRUE(metadata.fields().contains("shard_id"));
  EXPECT_EQ(metadata.fields().at("shard_id").string_value(), "shard-myapp-us-west-2");
}

// Test multiple metadata targets with different capture groups
TEST_F(SniToMetadataFilterTest, MultipleCaptureGroupsInDifferentTargets) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$)");

  // First target: app name
  auto* target1 = rule->add_metadata_targets();
  target1->set_metadata_key("app_name");
  target1->set_metadata_namespace("test.namespace");
  target1->set_metadata_value("\\1");

  // Second target: region
  auto* target2 = rule->add_metadata_targets();
  target2->set_metadata_key("region");
  target2->set_metadata_namespace("test.namespace");
  target2->set_metadata_value("\\2");

  // Third target: environment
  auto* target3 = rule->add_metadata_targets();
  target3->set_metadata_key("environment");
  target3->set_metadata_namespace("test.namespace");
  target3->set_metadata_value("\\3");

  // Fourth target: combined value
  auto* target4 = rule->add_metadata_targets();
  target4->set_metadata_key("combined");
  target4->set_metadata_namespace("test.namespace");
  target4->set_metadata_value("\\1-in-\\2-\\3");

  setUpFilter(config);

  ON_CALL(connection_, requestedServerName())
      .WillByDefault(Return("webapp.us-east-1.test.example.com"));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify all metadata was set correctly
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("test.namespace"));

  const auto& metadata = filter_metadata.at("test.namespace");
  ASSERT_TRUE(metadata.fields().contains("app_name"));
  ASSERT_TRUE(metadata.fields().contains("region"));
  ASSERT_TRUE(metadata.fields().contains("environment"));
  ASSERT_TRUE(metadata.fields().contains("combined"));

  EXPECT_EQ(metadata.fields().at("app_name").string_value(), "webapp");
  EXPECT_EQ(metadata.fields().at("region").string_value(), "us-east-1");
  EXPECT_EQ(metadata.fields().at("environment").string_value(), "test");
  EXPECT_EQ(metadata.fields().at("combined").string_value(), "webapp-in-us-east-1-test");
}

// Test no SNI available
TEST_F(SniToMetadataFilterTest, NoSniAvailable) {
  auto config = createBasicConfig();
  setUpFilter(config);

  // Mock empty SNI
  ON_CALL(connection_, requestedServerName()).WillByDefault(Return(""));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify no metadata was set
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  EXPECT_FALSE(filter_metadata.contains("envoy.filters.network.sni_to_metadata"));
}

// Test SNI that doesn't match pattern
TEST_F(SniToMetadataFilterTest, SniDoesNotMatchPattern) {
  auto config = createBasicConfig();
  setUpFilter(config);

  // Mock SNI that doesn't match the expected pattern
  ON_CALL(connection_, requestedServerName()).WillByDefault(Return("invalid.pattern.com"));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify no metadata was set
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  EXPECT_FALSE(filter_metadata.contains("envoy.filters.network.sni_to_metadata"));
}

// Test multiple rules with first matching rule applied
TEST_F(SniToMetadataFilterTest, MultipleRulesFirstMatchWins) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  // First rule: matches example.com pattern
  auto* rule1 = config.add_connection_rules();
  auto* pattern1 = rule1->mutable_pattern();
  pattern1->mutable_google_re2();
  pattern1->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$)");

  auto* target1 = rule1->add_metadata_targets();
  target1->set_metadata_key("source");
  target1->set_metadata_namespace("test.namespace");
  target1->set_metadata_value("example");

  // Second rule: matches any pattern with 3 parts
  auto* rule2 = config.add_connection_rules();
  auto* pattern2 = rule2->mutable_pattern();
  pattern2->mutable_google_re2();
  pattern2->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)$)");

  auto* target2 = rule2->add_metadata_targets();
  target2->set_metadata_key("source");
  target2->set_metadata_namespace("test.namespace");
  target2->set_metadata_value("generic");

  setUpFilter(config);

  ON_CALL(connection_, requestedServerName())
      .WillByDefault(Return("myapp.us-west-2.prod.example.com"));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify only the first rule's metadata was applied
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("test.namespace"));

  const auto& metadata = filter_metadata.at("test.namespace");
  ASSERT_TRUE(metadata.fields().contains("source"));
  EXPECT_EQ(metadata.fields().at("source").string_value(), "example");
}

// Test metadata target without metadata_value uses full SNI
TEST_F(SniToMetadataFilterTest, NoMetadataValueUsesFullSni) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$)");

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("full_sni");
  target->set_metadata_namespace("test.namespace");
  // metadata_value is intentionally left empty

  setUpFilter(config);

  std::string test_sni = "myapp.us-west-2.prod.example.com";
  ON_CALL(connection_, requestedServerName()).WillByDefault(Return(test_sni));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify the full SNI was used as metadata value
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("test.namespace"));

  const auto& metadata = filter_metadata.at("test.namespace");
  ASSERT_TRUE(metadata.fields().contains("full_sni"));
  EXPECT_EQ(metadata.fields().at("full_sni").string_value(), test_sni);
}

// Test default metadata namespace when not specified
TEST_F(SniToMetadataFilterTest, DefaultMetadataNamespace) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex(R"(^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$)");

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("app_name");
  // metadata_namespace is intentionally left empty to test default
  target->set_metadata_value("\\1");

  setUpFilter(config);

  ON_CALL(connection_, requestedServerName())
      .WillByDefault(Return("testapp.region.env.example.com"));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify the default namespace was used
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("envoy.filters.network.sni_to_metadata"));

  const auto& metadata = filter_metadata.at("envoy.filters.network.sni_to_metadata");
  ASSERT_TRUE(metadata.fields().contains("app_name"));
  EXPECT_EQ(metadata.fields().at("app_name").string_value(), "testapp");
}

// Test that filter only processes once per connection
TEST_F(SniToMetadataFilterTest, OnlyProcessOncePerConnection) {
  auto config = createBasicConfig();
  setUpFilter(config);

  ON_CALL(connection_, requestedServerName())
      .WillByDefault(Return("myapp.us-west-2.prod.example.com"));

  Buffer::OwnedImpl buffer1("test1");
  Buffer::OwnedImpl buffer2("test2");

  // First call should process
  Network::FilterStatus status1 = filter_->onData(buffer1, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status1);

  // Verify metadata was set
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("envoy.filters.network.sni_to_metadata"));

  const auto& metadata = filter_metadata.at("envoy.filters.network.sni_to_metadata");
  ASSERT_TRUE(metadata.fields().contains("shard_id"));
  EXPECT_EQ(metadata.fields().at("shard_id").string_value(), "shard-myapp-us-west-2");

  // Clear the metadata to verify second call doesn't set it again
  dynamic_metadata_.clear_filter_metadata();

  // Second call should skip processing
  Network::FilterStatus status2 = filter_->onData(buffer2, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status2);

  // Verify no metadata was set on second call
  const auto& filter_metadata2 = dynamic_metadata_.filter_metadata();
  EXPECT_FALSE(filter_metadata2.contains("envoy.filters.network.sni_to_metadata"));
}

// Test complex regex patterns
TEST_F(SniToMetadataFilterTest, ComplexRegexPatterns) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  // More complex pattern that captures service, version, region, and environment
  pattern->set_regex(R"(^([^.]+)-v(\d+)\.([^.]+)\.([^.]+)\.service\.company\.com$)");

  auto* target1 = rule->add_metadata_targets();
  target1->set_metadata_key("service_name");
  target1->set_metadata_namespace("company.metadata");
  target1->set_metadata_value("\\1");

  auto* target2 = rule->add_metadata_targets();
  target2->set_metadata_key("service_version");
  target2->set_metadata_namespace("company.metadata");
  target2->set_metadata_value("v\\2");

  auto* target3 = rule->add_metadata_targets();
  target3->set_metadata_key("deployment");
  target3->set_metadata_namespace("company.metadata");
  target3->set_metadata_value("\\1-v\\2-\\3-\\4");

  setUpFilter(config);

  ON_CALL(connection_, requestedServerName())
      .WillByDefault(Return("user-service-v42.us-west-2.production.service.company.com"));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify all metadata was set correctly
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("company.metadata"));

  const auto& metadata = filter_metadata.at("company.metadata");
  ASSERT_TRUE(metadata.fields().contains("service_name"));
  ASSERT_TRUE(metadata.fields().contains("service_version"));
  ASSERT_TRUE(metadata.fields().contains("deployment"));

  EXPECT_EQ(metadata.fields().at("service_name").string_value(), "user-service");
  EXPECT_EQ(metadata.fields().at("service_version").string_value(), "v42");
  EXPECT_EQ(metadata.fields().at("deployment").string_value(),
            "user-service-v42-us-west-2-production");
}

// Test rule without pattern captures full SNI
TEST_F(SniToMetadataFilterTest, NoPatternCapturesFullSni) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  auto* rule = config.add_connection_rules();
  // No pattern specified - should always match

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("full_sni");
  target->set_metadata_namespace("test.namespace");
  // metadata_value is intentionally left empty to use full SNI

  setUpFilter(config);

  std::string test_sni = "example.com";
  ON_CALL(connection_, requestedServerName()).WillByDefault(Return(test_sni));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify the full SNI was used as metadata value
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("test.namespace"));

  const auto& metadata = filter_metadata.at("test.namespace");
  ASSERT_TRUE(metadata.fields().contains("full_sni"));
  EXPECT_EQ(metadata.fields().at("full_sni").string_value(), test_sni);
}

// Test rule without pattern uses static metadata value
TEST_F(SniToMetadataFilterTest, NoPatternWithStaticValue) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  auto* rule = config.add_connection_rules();
  // No pattern specified - should always match

  auto* target1 = rule->add_metadata_targets();
  target1->set_metadata_key("service_type");
  target1->set_metadata_namespace("test.namespace");
  target1->set_metadata_value("web-service"); // Static value

  auto* target2 = rule->add_metadata_targets();
  target2->set_metadata_key("original_sni");
  target2->set_metadata_namespace("test.namespace");
  // metadata_value empty - should use full SNI

  setUpFilter(config);

  std::string test_sni = "api.example.com";
  ON_CALL(connection_, requestedServerName()).WillByDefault(Return(test_sni));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify both metadata values were set correctly
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("test.namespace"));

  const auto& metadata = filter_metadata.at("test.namespace");
  ASSERT_TRUE(metadata.fields().contains("service_type"));
  ASSERT_TRUE(metadata.fields().contains("original_sni"));

  EXPECT_EQ(metadata.fields().at("service_type").string_value(), "web-service");
  EXPECT_EQ(metadata.fields().at("original_sni").string_value(), test_sni);
}

// Test mixed rules: one with pattern, one without
TEST_F(SniToMetadataFilterTest, MixedRulesPatternAndNoPattern) {
  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;

  // First rule: no pattern (should always match, but we want it to not match this SNI)
  auto* rule1 = config.add_connection_rules();
  auto* pattern1 = rule1->mutable_pattern();
  pattern1->mutable_google_re2();
  pattern1->set_regex(R"(^api\.(.+)$)"); // Only matches SNIs starting with "api."

  auto* target1 = rule1->add_metadata_targets();
  target1->set_metadata_key("api_domain");
  target1->set_metadata_namespace("test.namespace");
  target1->set_metadata_value("\\1");

  // Second rule: no pattern (fallback - should match anything)
  auto* rule2 = config.add_connection_rules();
  // No pattern specified

  auto* target2 = rule2->add_metadata_targets();
  target2->set_metadata_key("fallback");
  target2->set_metadata_namespace("test.namespace");
  target2->set_metadata_value("default-service");

  setUpFilter(config);

  // Test with SNI that doesn't match first rule
  std::string test_sni = "webapp.example.com";
  ON_CALL(connection_, requestedServerName()).WillByDefault(Return(test_sni));

  Buffer::OwnedImpl buffer("test");

  Network::FilterStatus status = filter_->onData(buffer, false);
  EXPECT_EQ(Network::FilterStatus::Continue, status);

  // Verify only the fallback rule was applied (first matching rule wins)
  const auto& filter_metadata = dynamic_metadata_.filter_metadata();
  ASSERT_TRUE(filter_metadata.contains("test.namespace"));

  const auto& metadata = filter_metadata.at("test.namespace");
  EXPECT_FALSE(metadata.fields().contains("api_domain")); // First rule didn't match
  ASSERT_TRUE(metadata.fields().contains("fallback"));    // Second rule matched

  EXPECT_EQ(metadata.fields().at("fallback").string_value(), "default-service");
}

// Test invalid regex pattern returns error
TEST_F(SniToMetadataFilterTest, InvalidRegexReturnsError) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter config;
  auto* rule = config.add_connection_rules();
  auto* pattern = rule->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex("[invalid regex pattern"); // Missing closing bracket

  auto* target = rule->add_metadata_targets();
  target->set_metadata_key("test_key");
  target->set_metadata_namespace("test.namespace");
  target->set_metadata_value("\\1");

  // This should fail to create the config
  EXPECT_THROW(
      {
        auto config_shared =
            std::make_shared<Config>(config, context.serverFactoryContext().regexEngine());
      },
      EnvoyException);
}

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy