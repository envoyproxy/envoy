#include <memory>
#include <string>

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test sampler config json parsing
TEST(SamplerConfigTest, TestParsing) {
  // default_root_spans_per_minute not set, ROOT_SPANS_PER_MINUTE_DEFAULT should be used
  SamplerConfig config(0);
  EXPECT_TRUE(config.parse("{\n \"rootSpansPerMinute\" : 2000 \n }"));
  EXPECT_EQ(config.getRootSpansPerMinute(), 2000u);
  EXPECT_TRUE(config.parse("{\n \"rootSpansPerMinute\" : 10000 \n }"));
  EXPECT_EQ(config.getRootSpansPerMinute(), 10000u);

  // unexpected json, default value should be used
  EXPECT_FALSE(config.parse("{}"));
  EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);

  EXPECT_FALSE(config.parse(""));
  EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);

  EXPECT_FALSE(config.parse("\\"));
  EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);

  EXPECT_FALSE(config.parse(" { "));
  EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);

  EXPECT_FALSE(config.parse("{\n \"rootSpansPerMinute\" : 10000 ")); // closing } is missing
  EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
}

// Test sampler config default root spans per minute
TEST(SamplerConfigTest, TestDefaultConfig) {
  {
    SamplerConfig config(0);
    EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
    EXPECT_FALSE(config.parse(" { ")); // parse invalid json, default value should still be used
    EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
  }
  {
    SamplerConfig config(900);
    EXPECT_EQ(config.getRootSpansPerMinute(), 900);
    EXPECT_FALSE(config.parse(" { "));
    EXPECT_EQ(config.getRootSpansPerMinute(), 900);
  }
  {
    SamplerConfig config(SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
    EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
    EXPECT_FALSE(config.parse(" { "));
    EXPECT_EQ(config.getRootSpansPerMinute(), SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
