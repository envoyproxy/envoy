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

TEST(MetadataTest, MatchNullValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label").set_string_value("test");
  Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_null_value(ProtobufWkt::NullValue::NULL_VALUE);

  envoy::api::v2::core::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path("label");

  matcher.add_values()->set_exact_match("test");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_null_match(false);
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_null_match(true);
  EXPECT_TRUE(Metadata::match(matcher, metadata));
}

TEST(MetadataTest, MatchNumberValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label").set_string_value("test");
  Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label").set_number_value(9);

  envoy::api::v2::core::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path("label");

  matcher.add_values()->set_exact_match("test");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_number_match(1);
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_number_match(9);
  EXPECT_TRUE(Metadata::match(matcher, metadata));
}

TEST(MetadataTest, MatchStringExactValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label").set_string_value("test");
  Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label").set_string_value("prod");

  envoy::api::v2::core::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path("label");

  matcher.add_values()->set_exact_match("test");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_exact_match("prod");
  EXPECT_TRUE(Metadata::match(matcher, metadata));
}

TEST(MetadataTest, MatchStringPrefixValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label").set_string_value("test");
  Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label").set_string_value("prodabc");

  envoy::api::v2::core::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path("label");

  matcher.add_values()->set_exact_match("test");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_prefix_match("prodx");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_prefix_match("prod");
  EXPECT_TRUE(Metadata::match(matcher, metadata));
}

TEST(MetadataTest, MatchStringSuffixValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label").set_string_value("test");
  Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label").set_string_value("abcprod");

  envoy::api::v2::core::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path("label");

  matcher.add_values()->set_exact_match("test");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_suffix_match("prodx");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_suffix_match("prod");
  EXPECT_TRUE(Metadata::match(matcher, metadata));
  ;
}

TEST(MetadataTest, MatchBoolValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label").set_string_value("test");
  Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label").set_bool_value(true);

  envoy::api::v2::core::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path("label");

  matcher.add_values()->set_exact_match("test");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_bool_match(false);
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_bool_match(true);
  EXPECT_TRUE(Metadata::match(matcher, metadata));
}

TEST(MetadataTest, MatchPresentValue) {
  envoy::api::v2::core::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label").set_string_value("test");
  Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label").set_number_value(1);

  envoy::api::v2::core::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path("label");

  matcher.add_values()->set_exact_match("test");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_present_match(false);
  EXPECT_FALSE(Metadata::match(matcher, metadata));
  matcher.add_values()->set_present_match(true);
  EXPECT_TRUE(Metadata::match(matcher, metadata));

  matcher.clear_path();
  matcher.add_path("unknown");
  EXPECT_FALSE(Metadata::match(matcher, metadata));
}

} // namespace
} // namespace Config
} // namespace Envoy
