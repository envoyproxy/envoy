#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_tag.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class DynatraceTagTest : public ::testing::Test {};

TEST(DynatraceTagTest, ValidTag) {
  DynatraceTag new_tag = DynatraceTag::create("fw4;0;0;0;0;0;10;7b;3b1a;8h0101");

  EXPECT_EQ(new_tag.isValid(), true);
  EXPECT_EQ(new_tag.isIgnored(), false);
  EXPECT_EQ(new_tag.getSamplingExponent(), 10);
  EXPECT_EQ(new_tag.asString(), "fw4;0;0;0;0;0;10;7b;3b1a;8h0101");
}

TEST(DynatraceTagTest, IgnoredFieldSet) {
  DynatraceTag new_tag = DynatraceTag::create("fw4;0;0;0;0;1;10;7b");

  EXPECT_EQ(new_tag.isValid(), true);
  EXPECT_EQ(new_tag.isIgnored(), true);
  EXPECT_EQ(new_tag.getSamplingExponent(), 10);
  EXPECT_EQ(new_tag.asString(), "fw4;0;0;0;0;1;10;7b");
}

TEST(DynatraceTagTest, AtmTcrExtension) {
  DynatraceTag new_tag =
      DynatraceTag::create(0, 1, 0, TraceCaptureReason::create(TraceCaptureReason::Reason::Atm));

  EXPECT_EQ(new_tag.isValid(), true);
  EXPECT_EQ(new_tag.isIgnored(), false);
  EXPECT_EQ(new_tag.getSamplingExponent(), 1);
  EXPECT_EQ(new_tag.asString(), "fw4;0;0;0;0;0;1;0;3b1a;8h0101");
}

TEST(DynatraceTagTest, MultipleTcrExtensions) {
  DynatraceTag new_tag =
      DynatraceTag::create(0, 1, 0,
                           TraceCaptureReason::create(TraceCaptureReason::Reason::Atm |
                                                      TraceCaptureReason::Reason::Fixed |
                                                      TraceCaptureReason::Reason::Custom));

  EXPECT_EQ(new_tag.isValid(), true);
  EXPECT_EQ(new_tag.isIgnored(), false);
  EXPECT_EQ(new_tag.getSamplingExponent(), 1);
  EXPECT_EQ(new_tag.asString(), "fw4;0;0;0;0;0;1;0;e72f;8h0107");
}

class DynatraceTagInvalidTest : public ::testing::TestWithParam<std::string> {};

// Verify parsing of an invalid tags
TEST_P(DynatraceTagInvalidTest, InvalidTag) {
  DynatraceTag new_tag = DynatraceTag::create(GetParam());
  EXPECT_EQ(new_tag.isValid(), false);
  EXPECT_EQ(new_tag.asString(), "fw4;0;0;0;0;0;0;0");
}

INSTANTIATE_TEST_SUITE_P(
    InvalidTagsCase, DynatraceTagInvalidTest,
    ::testing::Values("fw4;0;0;0;0;0;10",    // missing path info
                      "fw4;0;0;0;0;0",       // missing sampling exponent and path info
                      "fw4;0;0;0;0",         // missing ignored, sampling exponent and path info
                      "fw3;0;0;0;0;0;10;7b", // invalid version
                      "",                    // empty string
                      "invalid_tag",         // completely invalid string
                      "fw400;0;0;0;10;7b"    // missing delimiter between fields
                      ));

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
