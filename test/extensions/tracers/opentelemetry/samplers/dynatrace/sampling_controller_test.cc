#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_sampler.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampling_controller.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

// helper to offer a value multiple times to SamplingController
void offerEntry(SamplingController& sc, const std::string& value, int count) {
  for (int i = 0; i < count; i++) {
    sc.offer(value);
  }
}

} // namespace

class TestSamplerConfigProvider : public SamplerConfigProvider {
public:
  TestSamplerConfigProvider(
      uint32_t root_spans_per_minute = SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT)
      : config(root_spans_per_minute) {}
  const SamplerConfig& getSamplerConfig() const override { return config; }
  SamplerConfig config;
};

// Test with multiple different sampling keys (StreamSummary size exceeded)
TEST(SamplingControllerTest, TestStreamSummarySizeExceeded) {
  auto scf = std::make_unique<TestSamplerConfigProvider>();
  SamplingController sc(std::move(scf));

  offerEntry(sc, "1", 2000);
  offerEntry(sc, "2", 1000);
  offerEntry(sc, "3", 750);
  offerEntry(sc, "4", 100);
  offerEntry(sc, "5", 50);
  // add unique sampling keys
  for (int64_t i = 0; i < 2100; i++) {
    sc.offer(std::to_string(i + 1000000));
  }

  sc.update();

  EXPECT_EQ(sc.getEffectiveCount(), 1110);
  EXPECT_EQ(sc.getSamplingState("1").getMultiplicity(), 128);
  EXPECT_EQ(sc.getSamplingState("2").getMultiplicity(), 64);
  EXPECT_EQ(sc.getSamplingState("3").getMultiplicity(), 64);
  EXPECT_EQ(sc.getSamplingState("4").getMultiplicity(), 8);
  EXPECT_EQ(sc.getSamplingState("5").getMultiplicity(), 4);
  EXPECT_EQ(sc.getSamplingState("1000000").getMultiplicity(), 2);
  EXPECT_EQ(sc.getSamplingState("1000001").getMultiplicity(), 2);
  EXPECT_EQ(sc.getSamplingState("1000002").getMultiplicity(), 2);
}

// Test with 0 root span per minute
TEST(SamplingControllerTest, TestWithZeroAllowedSpan) {
  // using 0 does not make sense, but let's ensure there is divide by zero
  auto scf = std::make_unique<TestSamplerConfigProvider>(0);
  SamplingController sc(std::move(scf));
  EXPECT_EQ(sc.getSamplingState("1").getMultiplicity(), 1);
  sc.update();
  EXPECT_EQ(sc.getSamplingState("1").getMultiplicity(), 1);
  offerEntry(sc, "1", 1);
  sc.update();
  EXPECT_EQ(sc.getSamplingState("1").getMultiplicity(), 1);
}

// Test with 1 root span per minute
TEST(SamplingControllerTest, TestWithOneAllowedSpan) {
  auto scf = std::make_unique<TestSamplerConfigProvider>(1);
  SamplingController sc(std::move(scf));
  sc.update();
  EXPECT_EQ(sc.getSamplingState("1").getExponent(), SamplingController::MAX_SAMPLING_EXPONENT);
  offerEntry(sc, "1", 1);
  EXPECT_EQ(sc.getSamplingState("1").getExponent(), SamplingController::MAX_SAMPLING_EXPONENT);
  sc.update();
  EXPECT_EQ(sc.getSamplingState("1").getMultiplicity(), 1);
}

// Test with StreamSummary size not exceeded
TEST(SamplingControllerTest, TestStreamSummarySizeNotExceeded) {
  auto scf = std::make_unique<TestSamplerConfigProvider>();
  SamplingController sc(std::move(scf));

  offerEntry(sc, "1", 8600);
  offerEntry(sc, "2", 5000);
  offerEntry(sc, "3", 4000);
  offerEntry(sc, "4", 4000);
  offerEntry(sc, "5", 3000);
  offerEntry(sc, "6", 30);
  offerEntry(sc, "7", 3);
  offerEntry(sc, "8", 1);

  sc.update();

  EXPECT_EQ(sc.getEffectiveCount(), 1074);
  EXPECT_EQ(sc.getSamplingState("1").getMultiplicity(), 64);
  EXPECT_EQ(sc.getSamplingState("2").getMultiplicity(), 32);
  EXPECT_EQ(sc.getSamplingState("3").getMultiplicity(), 32);
  EXPECT_EQ(sc.getSamplingState("4").getMultiplicity(), 16);
  EXPECT_EQ(sc.getSamplingState("5").getMultiplicity(), 8);
  EXPECT_EQ(sc.getSamplingState("6").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("7").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("8").getMultiplicity(), 1);
}

// Test with StreamSummary size not exceeded
TEST(SamplingControllerTest, TestStreamSummarySizeNotExceeded1) {
  auto scf = std::make_unique<TestSamplerConfigProvider>();
  SamplingController sc(std::move(scf));

  offerEntry(sc, "1", 7500);
  offerEntry(sc, "2", 1000);
  offerEntry(sc, "3", 1);
  offerEntry(sc, "4", 1);
  offerEntry(sc, "5", 1);
  for (int64_t i = 0; i < 11; i++) {
    sc.offer(std::to_string(i + 1000000));
  }

  sc.update();

  EXPECT_EQ(sc.getEffectiveCount(), 1451);
  EXPECT_EQ(sc.getSamplingState("1").getMultiplicity(), 8);
  EXPECT_EQ(sc.getSamplingState("2").getMultiplicity(), 2);
  EXPECT_EQ(sc.getSamplingState("3").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("4").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("5").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("1000000").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("1000001").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("1000002").getMultiplicity(), 1);
  EXPECT_EQ(sc.getSamplingState("1000003").getMultiplicity(), 1);
}

// Test using a sampler config having non-default root spans per minute
TEST(SamplingControllerTest, TestNonDefaultRootSpansPerMinute) {
  auto scf = std::make_unique<TestSamplerConfigProvider>();
  scf->config.parse("{\n \"rootSpansPerMinute\" : 100 \n }");
  SamplingController sc(std::move(scf));

  offerEntry(sc, "GET_xxxx", 300);
  offerEntry(sc, "POST_asdf", 200);
  offerEntry(sc, "GET_asdf", 100);

  sc.update();

  EXPECT_EQ(sc.getSamplingState("GET_xxxx").getExponent(), 3);
  EXPECT_EQ(sc.getSamplingState("GET_xxxx").getMultiplicity(), 8);

  EXPECT_EQ(sc.getSamplingState("POST_asdf").getExponent(), 2);
  EXPECT_EQ(sc.getSamplingState("POST_asdf").getMultiplicity(), 4);

  EXPECT_EQ(sc.getSamplingState("GET_asdf").getExponent(), 1);
  EXPECT_EQ(sc.getSamplingState("GET_asdf").getMultiplicity(), 2);
}

// Test warm up phase (no SamplingState available)
TEST(SamplingControllerTest, TestWarmup) {
  auto scf = std::make_unique<TestSamplerConfigProvider>();
  SamplingController sc(std::move(scf));

  // offer entries, but don't call update();
  // sampling exponents table will be empty
  // exponent will be calculated based on total count.
  // same exponent for both existing and non-existing keys.

  offerEntry(sc, "GET_0", 10);
  EXPECT_EQ(sc.getSamplingState("GET_0").getExponent(), 0);
  EXPECT_EQ(sc.getSamplingState("GET_1").getExponent(), 0);
  EXPECT_EQ(sc.getSamplingState("GET_2").getExponent(), 0);

  offerEntry(sc, "GET_1", 540);
  // threshold/2 reached, sampling exponent is set to 1
  EXPECT_EQ(sc.getSamplingState("GET_1").getExponent(), 1);
  EXPECT_EQ(sc.getSamplingState("GET_2").getExponent(), 1);
  EXPECT_EQ(sc.getSamplingState("GET_3").getExponent(), 1);

  offerEntry(sc, "GET_2", 300);
  EXPECT_EQ(sc.getSamplingState("GET_1").getExponent(), 1);
  EXPECT_EQ(sc.getSamplingState("GET_2").getExponent(), 1);
  EXPECT_EQ(sc.getSamplingState("GET_123").getExponent(), 1);

  offerEntry(sc, "GET_4", 550);
  EXPECT_EQ(sc.getSamplingState("GET_1").getExponent(), 2);
  EXPECT_EQ(sc.getSamplingState("GET_4").getExponent(), 2);
  EXPECT_EQ(sc.getSamplingState("GET_234").getExponent(), 2);

  offerEntry(sc, "GET_5", 1000);
  EXPECT_EQ(sc.getSamplingState("GET_1").getExponent(), 4);
  EXPECT_EQ(sc.getSamplingState("GET_5").getExponent(), 4);
  EXPECT_EQ(sc.getSamplingState("GET_456").getExponent(), 4);

  offerEntry(sc, "GET_6", 2000);
  EXPECT_EQ(sc.getSamplingState("GET_1").getExponent(), 8);
  EXPECT_EQ(sc.getSamplingState("GET_6").getExponent(), 8);
  EXPECT_EQ(sc.getSamplingState("GET_789").getExponent(), 8);
}

// Test getting sampling state from an empty SamplingController
TEST(SamplingControllerTest, TestEmpty) {
  auto scf = std::make_unique<TestSamplerConfigProvider>();
  SamplingController sc(std::move(scf));

  sc.update();
  // default SamplingState is expected
  EXPECT_EQ(sc.getSamplingState("GET_something").getExponent(), 0);
  EXPECT_EQ(sc.getSamplingState("GET_something").getMultiplicity(), 1);
}

// Test getting sampling state for an unknown key from a non-empty SamplingController
TEST(SamplingControllerTest, TestUnknown) {
  auto scf = std::make_unique<TestSamplerConfigProvider>();
  SamplingController sc(std::move(scf));

  sc.offer("key1");
  sc.update();

  EXPECT_EQ(sc.getSamplingState("key2").getExponent(), 0);
  EXPECT_EQ(sc.getSamplingState("key2").getMultiplicity(), 1);

  // Exceed capacity,
  for (uint32_t i = 0; i < SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT * 2; i++) {
    sc.offer("key1");
  }
  sc.update();
  // "key1" will get exponent 1
  EXPECT_EQ(sc.getSamplingState("key1").getExponent(), 1);
  // unknown "key2" will get the same exponent
  EXPECT_EQ(sc.getSamplingState("key2").getExponent(), 1);
}

// Test increasing and decreasing sampling exponent
TEST(SamplingStateTest, TestIncreaseDecrease) {
  SamplingState sst{};
  EXPECT_EQ(sst.getExponent(), 0);
  EXPECT_EQ(sst.getMultiplicity(), 1);

  sst.increaseExponent();
  EXPECT_EQ(sst.getExponent(), 1);
  EXPECT_EQ(sst.getMultiplicity(), 2);

  sst.increaseExponent();
  EXPECT_EQ(sst.getExponent(), 2);
  EXPECT_EQ(sst.getMultiplicity(), 4);

  for (int i = 0; i < 6; i++) {
    sst.increaseExponent();
  }
  EXPECT_EQ(sst.getExponent(), 8);
  EXPECT_EQ(sst.getMultiplicity(), 256);

  sst.decreaseExponent();
  EXPECT_EQ(sst.getExponent(), 7);
  EXPECT_EQ(sst.getMultiplicity(), 128);
}

// Test SamplingState shouldSample()
TEST(SamplingStateTest, TestShouldSample) {
  // default sampling state should sample every request
  SamplingState sst{};
  EXPECT_TRUE(sst.shouldSample(1234));
  EXPECT_TRUE(sst.shouldSample(2345));
  EXPECT_TRUE(sst.shouldSample(3456));
  EXPECT_TRUE(sst.shouldSample(4567));

  // exponent 1, multiplicity 2,
  // we are using % for sampling decision, so even (=not odd) random numbers should be sampled
  sst.increaseExponent();
  EXPECT_TRUE(sst.shouldSample(22));
  EXPECT_TRUE(sst.shouldSample(4444444));
  EXPECT_FALSE(sst.shouldSample(21));
  EXPECT_FALSE(sst.shouldSample(111111));

  for (int i = 0; i < 9; i++) {
    sst.increaseExponent();
  }
  // exponent 10, multiplicity 1024,
  EXPECT_TRUE(sst.shouldSample(1024));
  EXPECT_TRUE(sst.shouldSample(2048));
  EXPECT_TRUE(sst.shouldSample(4096));
  EXPECT_TRUE(sst.shouldSample(10240000000));
  EXPECT_FALSE(sst.shouldSample(1023));
  EXPECT_FALSE(sst.shouldSample(1025));
  EXPECT_FALSE(sst.shouldSample(2047));
  EXPECT_FALSE(sst.shouldSample(2049));
}

// Test creating sampling key used to identify a request
TEST(SamplingControllerTest, TestGetSamplingKey) {
  std::string key = SamplingController::getSamplingKey("somepath", "GET");
  EXPECT_STREQ(key.c_str(), "GET_somepath");

  key = SamplingController::getSamplingKey("somepath?withquery", "POST");
  EXPECT_STREQ(key.c_str(), "POST_somepath");

  key = SamplingController::getSamplingKey("anotherpath", "PUT");
  EXPECT_STREQ(key.c_str(), "PUT_anotherpath");

  key = SamplingController::getSamplingKey("", "PUT");
  EXPECT_STREQ(key.c_str(), "PUT_");

  key = SamplingController::getSamplingKey("anotherpath", "");
  EXPECT_STREQ(key.c_str(), "_anotherpath");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
