#include "envoy/extensions/retry/host/omit_host_metadata/v3alpha/omit_host_metadata_config.pb.h"
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

  envoy::extensions::retry::host::omit_host_metadata::v3alpha::OmitHostMetadataConfig config;
  auto* metadata_match = config.mutable_metadata_match();
  Envoy::Config::Metadata::mutableMetadataValue(
      *metadata_match, Envoy::Config::MetadataFilters::get().ENVOY_LB, "key")
      .set_string_value("value");
  auto empty = factory->createEmptyConfigProto();
  empty->MergeFrom(config);
  auto predicate = factory->createHostPredicate(*empty, 3);

  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();

  // Test: if the host doesn't have metadata, it should not be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3alpha::Metadata>()));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host));

  // Test: if host has matching metadata criteria, it should be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3alpha::Metadata>(
          TestUtility::parseYaml<envoy::config::core::v3alpha::Metadata>(
              R"EOF(
        filter_metadata:
          envoy.lb:
            key: "value"
      )EOF"))));

  ASSERT_TRUE(predicate->shouldSelectAnotherHost(*host));

  // Test: if host doesn't have matching metadata criteria, it should not be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3alpha::Metadata>(
          TestUtility::parseYaml<envoy::config::core::v3alpha::Metadata>(
              R"EOF(
        filter_metadata:
          envoy.lb:
            key1: "value1"
      )EOF"))));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host));

  // Test: if host metadata has matching key but not the value, it should not be rejected.
  ON_CALL(*host, metadata())
      .WillByDefault(Return(std::make_shared<envoy::config::core::v3alpha::Metadata>(
          TestUtility::parseYaml<envoy::config::core::v3alpha::Metadata>(
              R"EOF(
        filter_metadata:
          envoy.lb:
            key: "value1"
      )EOF"))));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host));
}
} // namespace
} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
