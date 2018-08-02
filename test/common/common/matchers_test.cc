#include "envoy/api/v2/core/base.pb.h"
#include "envoy/type/matcher/metadata.pb.h"
#include "envoy/type/matcher/number.pb.h"
#include "envoy/type/matcher/string.pb.h"

#include "common/common/matchers.h"
#include "common/config/metadata.h"
#include "common/protobuf/protobuf.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {
namespace {

TEST(MetadataTest, MatchNullValue) {
  envoy::api::v2::core::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_null_value(ProtobufWkt::NullValue::NULL_VALUE);

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_null_match();
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchDoubleValue) {
  envoy::api::v2::core::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_number_value(9);

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_double_match()->set_exact(1);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_double_match()->set_exact(9);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  auto r = matcher.mutable_value()->mutable_double_match()->mutable_range();
  r->set_start(9.1);
  r->set_end(10);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = matcher.mutable_value()->mutable_double_match()->mutable_range();
  r->set_start(8.9);
  r->set_end(9);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = matcher.mutable_value()->mutable_double_match()->mutable_range();
  r->set_start(9);
  r->set_end(9.1);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchStringExactValue) {
  envoy::api::v2::core::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_string_value("prod");

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_exact("prod");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchStringPrefixValue) {
  envoy::api::v2::core::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_string_value("prodabc");

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_prefix("prodx");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_prefix("prod");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchStringSuffixValue) {
  envoy::api::v2::core::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_string_value("abcprod");

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_suffix("prodx");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->mutable_string_match()->set_suffix("prod");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  ;
}

TEST(MetadataTest, MatchBoolValue) {
  envoy::api::v2::core::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_bool_value(true);

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_bool_match(false);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_bool_match(true);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

TEST(MetadataTest, MatchPresentValue) {
  envoy::api::v2::core::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "label")
      .set_string_value("test");
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.b", "label")
      .set_number_value(1);

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.b");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_present_match(false);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  matcher.mutable_value()->set_present_match(true);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  matcher.clear_path();
  matcher.add_path()->set_key("unknown");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
}

// Helper function to retrieve the reference of an entry in a ListMatcher from a MetadataMatcher.
envoy::type::matcher::ValueMatcher* listMatchEntry(envoy::type::matcher::MetadataMatcher* matcher) {
  return matcher->mutable_value()->mutable_list_match()->mutable_one_of();
}

TEST(MetadataTest, MatchStringListValue) {
  envoy::api::v2::core::Metadata metadata;
  ProtobufWkt::Value& metadataValue =
      Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "groups");
  ProtobufWkt::ListValue* values = metadataValue.mutable_list_value();
  values->add_values()->set_string_value("first");
  values->add_values()->set_string_value("second");
  values->add_values()->set_string_value("third");

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.a");
  matcher.add_path()->set_key("groups");

  listMatchEntry(&matcher)->mutable_string_match()->set_exact("second");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_prefix("fi");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_suffix("rd");
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_exact("fourth");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_string_match()->set_prefix("none");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  values->clear_values();
  metadataValue.Clear();
}

TEST(MetadataTest, MatchBoolListValue) {
  envoy::api::v2::core::Metadata metadata;
  ProtobufWkt::Value& metadataValue =
      Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "groups");
  ProtobufWkt::ListValue* values = metadataValue.mutable_list_value();
  values->add_values()->set_bool_value(false);
  values->add_values()->set_bool_value(false);

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.a");
  matcher.add_path()->set_key("groups");

  listMatchEntry(&matcher)->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->set_bool_match(true);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->set_bool_match(false);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  values->clear_values();
  metadataValue.Clear();
}

TEST(MetadataTest, MatchDoubleListValue) {
  envoy::api::v2::core::Metadata metadata;
  ProtobufWkt::Value& metadataValue =
      Envoy::Config::Metadata::mutableMetadataValue(metadata, "envoy.filter.a", "groups");
  ProtobufWkt::ListValue* values = metadataValue.mutable_list_value();
  values->add_values()->set_number_value(10);
  values->add_values()->set_number_value(23);

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("envoy.filter.a");
  matcher.add_path()->set_key("groups");

  listMatchEntry(&matcher)->mutable_string_match()->set_exact("test");
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->set_bool_match(true);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_double_match()->set_exact(9);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));
  listMatchEntry(&matcher)->mutable_double_match()->set_exact(10);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  auto r = listMatchEntry(&matcher)->mutable_double_match()->mutable_range();
  r->set_start(10);
  r->set_end(15);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = listMatchEntry(&matcher)->mutable_double_match()->mutable_range();
  r->set_start(20);
  r->set_end(24);
  EXPECT_TRUE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  r = listMatchEntry(&matcher)->mutable_double_match()->mutable_range();
  r->set_start(24);
  r->set_end(26);
  EXPECT_FALSE(Envoy::Matchers::MetadataMatcher(matcher).match(metadata));

  values->clear_values();
  metadataValue.Clear();
}

} // namespace
} // namespace Matcher
} // namespace Envoy
