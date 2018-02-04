#include "common/config/metadata.h"
#include "common/config/well_known_names.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(MetadataTest, MetadataValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, MetadataFilters::get().ENVOY_LB,
                                 MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  EXPECT_TRUE(Metadata::metadataValue(metadata, MetadataFilters::get().ENVOY_LB,
                                      MetadataEnvoyLbKeys::get().CANARY)
                  .bool_value());
  EXPECT_FALSE(Metadata::metadataValue(metadata, "foo", "bar").bool_value());
  EXPECT_FALSE(
      Metadata::metadataValue(metadata, MetadataFilters::get().ENVOY_LB, "bar").bool_value());
}

} // namespace
} // namespace Config
} // namespace Envoy
