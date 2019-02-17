#include <string>

#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(UUIDUtilsTest, mod) {
  uint64_t result;
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

    const char c = uuid[19];
    ASSERT_TRUE(uuid[14] == '4');                              // UUID version 4 (random)
    ASSERT_TRUE(c == '8' || c == '9' || c == 'a' || c == 'b'); // UUID variant 1 (RFC4122)

    uint64_t value;
    ASSERT_TRUE(UuidUtils::uuidModBy(uuid, value, mod));

    if (value < required_percentage) {
      interesting_samples++;
    }
    total_samples++;
  }

  EXPECT_NEAR(required_percentage / 100.0, interesting_samples * 1.0 / total_samples, 0.002);
}

TEST(UUIDUtilsTest, DISABLED_benchmark) {
  Runtime::RandomGeneratorImpl random;

  for (int i = 0; i < 100000000; ++i) {
    random.uuid();
  }
}

TEST(UUIDUtilsTest, setAndCheckTraceable) {
  Runtime::RandomGeneratorImpl random;

  std::string uuid = random.uuid();
  EXPECT_EQ(UuidTraceStatus::NoTrace, UuidUtils::isTraceableUuid(uuid));

  EXPECT_TRUE(UuidUtils::setTraceableUuid(uuid, UuidTraceStatus::Sampled));
  EXPECT_EQ(UuidTraceStatus::Sampled, UuidUtils::isTraceableUuid(uuid));

  EXPECT_TRUE(UuidUtils::setTraceableUuid(uuid, UuidTraceStatus::Client));
  EXPECT_EQ(UuidTraceStatus::Client, UuidUtils::isTraceableUuid(uuid));

  EXPECT_TRUE(UuidUtils::setTraceableUuid(uuid, UuidTraceStatus::Forced));
  EXPECT_EQ(UuidTraceStatus::Forced, UuidUtils::isTraceableUuid(uuid));

  EXPECT_TRUE(UuidUtils::setTraceableUuid(uuid, UuidTraceStatus::NoTrace));
  EXPECT_EQ(UuidTraceStatus::NoTrace, UuidUtils::isTraceableUuid(uuid));

  std::string invalid_uuid = "";
  EXPECT_FALSE(UuidUtils::setTraceableUuid(invalid_uuid, UuidTraceStatus::Forced));
}
} // namespace Envoy
