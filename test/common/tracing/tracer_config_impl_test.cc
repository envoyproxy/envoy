#include "source/common/tracing/tracer_config_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Tracing {
namespace {

TEST(ConnectionManagerTracingConfigImplTest, SimpleTest) {
  {
    envoy::config::core::v3::TrafficDirection traffic_direction =
        envoy::config::core::v3::TrafficDirection::INBOUND;
    ConnectionManagerTracingConfigProto tracing_config;
    tracing_config.mutable_client_sampling()->set_value(50);
    tracing_config.mutable_random_sampling()->set_value(50);
    tracing_config.mutable_overall_sampling()->set_value(50);
    tracing_config.set_verbose(true);
    tracing_config.mutable_max_path_tag_length()->set_value(128);

    auto* custom_tag = tracing_config.add_custom_tags();
    custom_tag->set_tag("foo");
    custom_tag->mutable_literal()->set_value("bar");

    ConnectionManagerTracingConfigImpl config(traffic_direction, tracing_config);

    EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());
    EXPECT_EQ(true, config.verbose());
    EXPECT_EQ(128, config.maxPathTagLength());
    EXPECT_EQ(1, config.getCustomTags().size());

    EXPECT_EQ(50, config.getClientSampling().numerator());
    EXPECT_EQ(5000, config.getRandomSampling().numerator());
    EXPECT_EQ(5000, config.getOverallSampling().numerator());
  }

  {
    envoy::config::core::v3::TrafficDirection traffic_direction =
        envoy::config::core::v3::TrafficDirection::OUTBOUND;
    ConnectionManagerTracingConfigProto tracing_config;
    tracing_config.set_verbose(true);

    auto* custom_tag = tracing_config.add_custom_tags();
    custom_tag->set_tag("foo");
    custom_tag->mutable_literal()->set_value("bar");

    ConnectionManagerTracingConfigImpl config(traffic_direction, tracing_config);

    EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());
    EXPECT_EQ(true, config.verbose());
    EXPECT_EQ(256, config.maxPathTagLength());
    EXPECT_EQ(1, config.getCustomTags().size());

    EXPECT_EQ(100, config.getClientSampling().numerator());
    EXPECT_EQ(10000, config.getRandomSampling().numerator());
    EXPECT_EQ(10000, config.getOverallSampling().numerator());
  }
}

} // namespace
} // namespace Tracing
} // namespace Envoy
