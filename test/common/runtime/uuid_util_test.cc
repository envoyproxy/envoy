#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"

TEST(UUIDUtilsTest, mod) {
  uint16_t result;
  EXPECT_TRUE(UuidUtils::uuidModBy("00000000-0000-0000-0000-000000000000", result, 100));
  EXPECT_EQ(0, result);

  EXPECT_TRUE(UuidUtils::uuidModBy("00000001-0000-0000-0000-000000000000", result, 100));
  EXPECT_EQ(1, result);

  EXPECT_TRUE(UuidUtils::uuidModBy("0000000f-0000-0000-0000-00000000000a", result, 100));
  EXPECT_EQ(15, result);

  EXPECT_FALSE(UuidUtils::uuidModBy("", result, 100));

  EXPECT_TRUE(UuidUtils::uuidModBy("000000ff-0000-0000-0000-000000000000", result, 100));
  EXPECT_EQ(55, result);

  EXPECT_TRUE(UuidUtils::uuidModBy("000000ff-0000-0000-0000-000000000000", result, 10000));
  EXPECT_EQ(255, result);

  EXPECT_TRUE(UuidUtils::uuidModBy("a0090100-0012-0110-00ff-0c00400600ff", result, 137));
  EXPECT_EQ(8, result);

  EXPECT_TRUE(UuidUtils::uuidModBy("ffffffff-0012-0110-00ff-0c00400600ff", result, 100));
  EXPECT_EQ(95, result);

  EXPECT_TRUE(UuidUtils::uuidModBy("ffffffff-0012-0110-00ff-0c00400600ff", result, 10000));
  EXPECT_EQ(7295, result);
}

TEST(UUIDUtilsTest, checkDistribution) {
  Runtime::RandomGeneratorImpl random;

  const int mod = 100;
  const int required_percentage = 11;
  int total_samples = 0;
  int interesting_samples = 0;

  for (int i = 0; i < 500000; ++i) {
    std::string uuid = random.uuid();

    uint16_t value;
    UuidUtils::uuidModBy(uuid, value, mod);

    if (value < required_percentage) {
      interesting_samples++;
    }
    total_samples++;
  }

  EXPECT_NEAR(required_percentage / 100.0, interesting_samples * 1.0 / total_samples, 0.002);
}

TEST(UUIDUtilsTest, setAndCheckTraceable) {
  Runtime::RandomGeneratorImpl random;

  std::string uuid = random.uuid();
  TraceDecision decision = UuidUtils::isTraceableUuid(uuid);
  EXPECT_FALSE(decision.is_traced);

  EXPECT_TRUE(UuidUtils::setTraceableUuid(uuid, TraceReason::Sampled));
  decision = UuidUtils::isTraceableUuid(uuid);
  EXPECT_TRUE(decision.is_traced);
  EXPECT_EQ(decision.reason, Optional<TraceReason>(TraceReason::Sampled));

  EXPECT_TRUE(UuidUtils::setTraceableUuid(uuid, TraceReason::Client));
  decision = UuidUtils::isTraceableUuid(uuid);
  EXPECT_TRUE(decision.is_traced);
  EXPECT_EQ(decision.reason, Optional<TraceReason>(TraceReason::Client));

  EXPECT_TRUE(UuidUtils::setTraceableUuid(uuid, TraceReason::Forced));
  decision = UuidUtils::isTraceableUuid(uuid);
  EXPECT_TRUE(decision.is_traced);
  EXPECT_EQ(decision.reason, Optional<TraceReason>(TraceReason::Forced));

  std::string invalid_uuid = "";
  EXPECT_FALSE(UuidUtils::setTraceableUuid(invalid_uuid, TraceReason::Forced));
}
