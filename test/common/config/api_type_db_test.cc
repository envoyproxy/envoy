#include "common/config/api_type_db.h"

// For proto descriptors only
#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.h"
#include "envoy/config/filter/http/ip_tagging/v3alpha/ip_tagging.pb.h"

#include "gtest/gtest.h"
#include "udpa/type/v1/typed_struct.pb.h"

namespace Envoy {
namespace Config {
namespace {

TEST(ApiTypeDbTest, All) {
  EXPECT_EQ(nullptr, ApiTypeDb::inferEarlierVersionDescriptor("foo", {}, ""));
  EXPECT_EQ(nullptr, ApiTypeDb::inferEarlierVersionDescriptor("envoy.ip_tagging", {}, ""));

  // Struct upgrade to v3alpha.
  {
    const auto* desc = ApiTypeDb::inferEarlierVersionDescriptor(
        "envoy.ip_tagging", {}, "envoy.config.filter.http.ip_tagging.v3alpha.IPTagging");
    EXPECT_EQ("envoy.config.filter.http.ip_tagging.v2.IPTagging", desc->full_name());
  }

  // Any upgrade from v2 to v3alpha.
  {
    ProtobufWkt::Any typed_config;
    typed_config.set_type_url("envoy.config.filter.http.ip_tagging.v2.IPTagging");
    const auto* desc = ApiTypeDb::inferEarlierVersionDescriptor(
        "envoy.ip_tagging", typed_config, "envoy.config.filter.http.ip_tagging.v3alpha.IPTagging");
    EXPECT_EQ("envoy.config.filter.http.ip_tagging.v2.IPTagging", desc->full_name());
  }

  // There is no upgrade for same Any and target type URL.
  {
    ProtobufWkt::Any typed_config;
    typed_config.set_type_url("envoy.config.filter.http.ip_tagging.v3alpha.IPTagging");
    EXPECT_EQ(nullptr, ApiTypeDb::inferEarlierVersionDescriptor(
                           "envoy.ip_tagging", typed_config,
                           "envoy.config.filter.http.ip_tagging.v3alpha.IPTagging"));
  }

  // TypedStruct upgrade from v2 to v3alpha.
  {
    ProtobufWkt::Any typed_config;
    udpa::type::v1::TypedStruct typed_struct;
    typed_struct.set_type_url("envoy.config.filter.http.ip_tagging.v2.IPTagging");
    typed_config.PackFrom(typed_struct);
    const auto* desc = ApiTypeDb::inferEarlierVersionDescriptor(
        "envoy.ip_tagging", typed_config, "envoy.config.filter.http.ip_tagging.v3alpha.IPTagging");
    EXPECT_EQ("envoy.config.filter.http.ip_tagging.v2.IPTagging", desc->full_name());
  }

  // There is no upgrade for same TypedStruct and target type URL.
  {
    ProtobufWkt::Any typed_config;
    udpa::type::v1::TypedStruct typed_struct;
    typed_struct.set_type_url(
        "type.googleapis.com/envoy.config.filter.http.ip_tagging.v3alpha.IPTagging");
    typed_config.PackFrom(typed_struct);
    EXPECT_EQ(nullptr, ApiTypeDb::inferEarlierVersionDescriptor(
                           "envoy.ip_tagging", typed_config,
                           "envoy.config.filter.http.ip_tagging.v3alpha.IPTagging"));
  }

  // There is no upgrade for v2.
  EXPECT_EQ(nullptr,
            ApiTypeDb::inferEarlierVersionDescriptor(
                "envoy.ip_tagging", {}, "envoy.config.filter.http.ip_tagging.v2.IPTagging"));
}

} // namespace
} // namespace Config
} // namespace Envoy
