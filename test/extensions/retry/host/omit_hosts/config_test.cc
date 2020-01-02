#include "envoy/config/retry/omit_hosts/omit_hosts_config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/host/omit_hosts/omit_hosts.h"
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
      RetryHostPredicateValues::get().OmitHostsPredicate);

  ASSERT_NE(nullptr, factory);

  envoy::config::retry::omit_hosts::OmitHostsConfig config;
  auto* metadata_match = config.mutable_metadata_match();
  Envoy::Config::Metadata::mutableMetadataValue(
      *metadata_match, Envoy::Config::MetadataFilters::get().ENVOY_LB, "key")
      .set_string_value("value");
  auto empty = factory->createEmptyConfigProto();
  empty->MergeFrom(config);
  auto predicate = factory->createHostPredicate(*empty, 3);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();

  // Test: if the host doesn't have metadata, the host shouldn't be rejected
  ON_CALL(*host1, metadata())
      .WillByDefault(Return(std::make_shared<envoy::api::v2::core::Metadata>()));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host1));

  // Test: if host has metadata matching the criteria to reject the host
  ON_CALL(*host1, metadata())
      .WillByDefault(Return(std::make_shared<envoy::api::v2::core::Metadata>(
          TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
              R"EOF(
        filter_metadata:
          envoy.lb:
            key: "value"
      )EOF"))));

  ASSERT_TRUE(predicate->shouldSelectAnotherHost(*host1));

  // Test: if host doesn't have metadata matching the criteria to reject the host
  ON_CALL(*host2, metadata())
      .WillByDefault(Return(std::make_shared<envoy::api::v2::core::Metadata>(
          TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
              R"EOF(
        filter_metadata:
          envoy.lb:
            key1: "value1"
      )EOF"))));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host2));
}
} // namespace
} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
