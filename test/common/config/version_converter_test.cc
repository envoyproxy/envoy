#include "common/config/version_converter.h"

#include "test/common/config/version_converter.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(VersionConverterTest, All) {
  test::common::config::PreviousVersion previous_version;
  const std::string previous_version_yaml = R"EOF(
    string_field: foo
    bytes_field: YWJjMTIzIT8kKiYoKSctPUB+
    int32_field: -1
    int64_field: -2
    uint32_field: 1
    uint64_field: 2
    double_field: 1.0
    float_field: 2.0
    bool_field: true

    enum_field: PREV_OTHER_VALUE

    nested_field:
      any_field:
        "@type": type.googleapis.com/google.protobuf.UInt32Value
        value: 42

    repeated_scalar_field: ["foo", "bar"]
    repeated_nested_field:
    - any_field:
        "@type": type.googleapis.com/google.protobuf.UInt32Value
        value: 1
    - any_field:
        "@type": type.googleapis.com/google.protobuf.UInt64Value
        value: 2

    deprecated_field: 1
    enum_field_with_deprecated_value: PREV_DEPRECATED_VALUE
  )EOF";
  TestUtility::loadFromYaml(previous_version_yaml, previous_version);
  test::common::config::NextVersion next_version;

  VersionConverter::upgrade(previous_version, next_version);

  // Singleton scalars.
  EXPECT_EQ(previous_version.string_field(), next_version.string_field());
  EXPECT_EQ(previous_version.bytes_field(), next_version.bytes_field());
  EXPECT_EQ(previous_version.int32_field(), next_version.int32_field());
  EXPECT_EQ(previous_version.int64_field(), next_version.int64_field());
  EXPECT_EQ(previous_version.uint32_field(), next_version.uint32_field());
  EXPECT_EQ(previous_version.uint64_field(), next_version.uint64_field());
  EXPECT_EQ(previous_version.double_field(), next_version.double_field());
  EXPECT_EQ(previous_version.float_field(), next_version.float_field());
  EXPECT_EQ(previous_version.bool_field(), next_version.bool_field());
  EXPECT_EQ(previous_version.enum_field(), next_version.enum_field());

  // Singleton nested message.
  EXPECT_THAT(previous_version.nested_field().any_field(),
              ProtoEq(next_version.nested_field().any_field()));

  // Repeated entities.
  EXPECT_EQ(previous_version.repeated_scalar_field().size(),
            next_version.repeated_scalar_field().size());
  for (int n = 0; n < previous_version.repeated_scalar_field().size(); ++n) {
    EXPECT_EQ(previous_version.repeated_scalar_field()[n], next_version.repeated_scalar_field()[n]);
  }
  EXPECT_EQ(previous_version.repeated_nested_field().size(),
            next_version.repeated_nested_field().size());
  for (int n = 0; n < previous_version.repeated_nested_field().size(); ++n) {
    EXPECT_THAT(previous_version.repeated_nested_field()[n].any_field(),
                ProtoEq(next_version.repeated_nested_field()[n].any_field()));
  }

  // Deprecations.
  test::common::config::PreviousVersion deprecated;
  VersionConverter::unpackDeprecated(next_version, deprecated);
  EXPECT_EQ(previous_version.deprecated_field(), deprecated.deprecated_field());

  test::common::config::PreviousEnum enum_with_deprecated_value =
      static_cast<test::common::config::PreviousEnum>(
          next_version.enum_field_with_deprecated_value());
  EXPECT_EQ(enum_with_deprecated_value, test::common::config::PREV_DEPRECATED_VALUE);
}

} // namespace
} // namespace Config
} // namespace Envoy
