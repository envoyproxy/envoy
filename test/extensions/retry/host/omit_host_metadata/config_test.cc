#include "envoy/extensions/retry/host/omit_host_metadata/v3/omit_host_metadata_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/host/omit_host_metadata/omit_host_metadata.h"
#include "extensions/retry/host/well_known_names.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {
namespace {

TEST(OmitHostsRetryPredicateTest, PredicateTest) {
  auto factory = Registry::FactoryRegistry<Upstream::RetryHostPredicateFactory>::getFactory(
      RetryHostPredicateValues::get().OmitHostMetadataPredicate);

  ASSERT_NE(nullptr, factory);

  envoy::extensions::retry::host::omit_host_metadata::v3::OmitHostMetadataConfig config;
  auto empty = factory->createEmptyConfigProto();
  empty->MergeFrom(config);
  auto predicate = factory->createHostPredicate(*empty, 3);

  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();

  // Test: if no metadata match criteria defined, the host should not be rejected.
  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host));

  auto* metadata_match = config.mutable_metadata_match();
  Envoy::Config::Metadata::mutableMetadataValue(
      *metadata_match, Envoy::Config::MetadataFilters::get().ENVOY_LB, "key")
      .set_string_value("value");
  empty->MergeFrom(config);
  predicate = factory->createHostPredicate(*empty, 3);

  // Test: if host doesn't have metadata, it should not be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3::Metadata>()));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host));

  // Test: if host has matching metadata, it should be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3::Metadata>(
          TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
              R"EOF(
          filter_metadata:
            envoy.lb:
              key: "value"
        )EOF"))));

  ASSERT_TRUE(predicate->shouldSelectAnotherHost(*host));

  // Test: if host doesn't have matching metadata, it should not be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3::Metadata>(
          TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
              R"EOF(
          filter_metadata:
            envoy.lb:
              key1: "value1"
        )EOF"))));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host));

  // Test: if host metadata has matching key but not the value, it should not be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3::Metadata>(
          TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
              R"EOF(
          filter_metadata:
            envoy.lb:
              key: "value1"
        )EOF"))));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host));

  predicate->onHostAttempted(host);
}
} // namespace
} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
