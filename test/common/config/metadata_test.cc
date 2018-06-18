#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/utility.h"

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

TEST(MetadataTest, MetadataValuePath) {
  const std::string filter = "com.test";
  envoy::api::v2::core::Metadata metadata;
  std::vector<std::string> path{"test_obj", "inner_key"};
  // not found case
  EXPECT_EQ(Metadata::metadataValue(metadata, filter, path).kind_case(),
            ProtobufWkt::Value::KindCase::KIND_NOT_SET);
  ProtobufWkt::Struct& filter_struct = (*metadata.mutable_filter_metadata())[filter];
  auto obj = MessageUtil::keyValueStruct("inner_key", "inner_value");
  ProtobufWkt::Value val;
  *val.mutable_struct_value() = obj;
  (*filter_struct.mutable_fields())["test_obj"] = val;
  EXPECT_EQ(Metadata::metadataValue(metadata, filter, path).string_value(), "inner_value");
  // not found with longer path
  path.push_back("bad_key");
  EXPECT_EQ(Metadata::metadataValue(metadata, filter, path).kind_case(),
            ProtobufWkt::Value::KindCase::KIND_NOT_SET);
  // empty path returns not found
  EXPECT_EQ(Metadata::metadataValue(metadata, filter, std::vector<std::string>{}).kind_case(),
            ProtobufWkt::Value::KindCase::KIND_NOT_SET);
}

} // namespace
} // namespace Config
} // namespace Envoy
