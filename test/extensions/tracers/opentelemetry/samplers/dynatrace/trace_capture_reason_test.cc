#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/trace_capture_reason.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class TraceCaptureReasonTest : public ::testing::Test {};

TEST_F(TraceCaptureReasonTest, ParseSingleReasonATM) {
  auto tcr = TraceCaptureReason::create("0101");
  EXPECT_TRUE(tcr.isValid());
  EXPECT_EQ(tcr.bitmaskHex(), "01");
  auto reasons = tcr.toSpanAttributeValue();
  ASSERT_EQ(reasons.size(), 1);
  EXPECT_EQ(reasons[0], "atm");
}

TEST_F(TraceCaptureReasonTest, ParseSingleReasonFixed) {
  auto tcr = TraceCaptureReason::create("0102");
  EXPECT_TRUE(tcr.isValid());
  EXPECT_EQ(tcr.bitmaskHex(), "02");
  auto reasons = tcr.toSpanAttributeValue();
  ASSERT_EQ(reasons.size(), 1);
  EXPECT_EQ(reasons[0], "fixed");
}

TEST_F(TraceCaptureReasonTest, ParseMultipleReasonsATMFixed) {
  auto tcr = TraceCaptureReason::create("0103"); // ATM + Fixed
  EXPECT_TRUE(tcr.isValid());
  EXPECT_EQ(tcr.bitmaskHex(), "03");
  auto reasons = tcr.toSpanAttributeValue();
  ASSERT_EQ(reasons.size(), 2);
  EXPECT_EQ(reasons[0], "atm");
  EXPECT_EQ(reasons[1], "fixed");
}

TEST_F(TraceCaptureReasonTest, ParseMultipleReasonsAll) {
  auto tcr = TraceCaptureReason::create("0107"); // ATM + Fixed + Custom
  EXPECT_TRUE(tcr.isValid());
  EXPECT_EQ(tcr.bitmaskHex(), "07");
  auto reasons = tcr.toSpanAttributeValue();
  ASSERT_EQ(reasons.size(), 3);
  EXPECT_EQ(reasons[0], "atm");
  EXPECT_EQ(reasons[1], "fixed");
  EXPECT_EQ(reasons[2], "custom");
}

TEST_F(TraceCaptureReasonTest, ParseReasonRum) {
  auto tcr = TraceCaptureReason::create("0120"); // Rum only
  EXPECT_TRUE(tcr.isValid());
  EXPECT_EQ(tcr.bitmaskHex(), "20");
  auto reasons = tcr.toSpanAttributeValue();
  ASSERT_EQ(reasons.size(), 1);
  EXPECT_EQ(reasons[0], "rum");
}

TEST_F(TraceCaptureReasonTest, ParseMultipleReasonsATMCustomRum) {
  auto tcr = TraceCaptureReason::create("0125"); // ATM + Custom + Rum
  EXPECT_TRUE(tcr.isValid());
  EXPECT_EQ(tcr.bitmaskHex(), "25");
  auto reasons = tcr.toSpanAttributeValue();
  ASSERT_EQ(reasons.size(), 3);
  EXPECT_EQ(reasons[0], "atm");
  EXPECT_EQ(reasons[1], "custom");
  EXPECT_EQ(reasons[2], "rum");
}

TEST_F(TraceCaptureReasonTest, InvalidVersion) {
  auto tcr = TraceCaptureReason::create("0201"); // version 2 not supported
  EXPECT_FALSE(tcr.isValid());
}

TEST_F(TraceCaptureReasonTest, InvalidBitmask) {
  auto tcr = TraceCaptureReason::create("01FF"); // FF includes undefined bits
  EXPECT_FALSE(tcr.isValid());
}

TEST_F(TraceCaptureReasonTest, InvalidShortPayload) {
  auto tcr = TraceCaptureReason::create("01"); // too short
  EXPECT_FALSE(tcr.isValid());
}

TEST_F(TraceCaptureReasonTest, InvalidOddLengthPayload010) {
  auto tcr = TraceCaptureReason::create("010"); // odd number of chars
  EXPECT_FALSE(tcr.isValid());
}

TEST_F(TraceCaptureReasonTest, InvalidOddLengthPayload01010) {
  auto tcr = TraceCaptureReason::create("01010"); // odd number of chars
  EXPECT_FALSE(tcr.isValid());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
