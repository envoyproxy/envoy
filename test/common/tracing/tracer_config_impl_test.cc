#include "source/common/tracing/tracer_config_impl.h"

#include "test/mocks/stream_info/mocks.h"
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

    ConnectionManagerTracingConfig config(traffic_direction, tracing_config);

    EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());
    EXPECT_EQ(true, config.verbose());
    EXPECT_EQ(128, config.maxPathTagLength());
    EXPECT_EQ(1, config.getCustomTags().size());

    EXPECT_EQ(50, config.getClientSampling().numerator());
    EXPECT_EQ(5000, config.getRandomSampling().numerator());
    EXPECT_EQ(5000, config.getOverallSampling().numerator());

    EXPECT_EQ(config.operation_, nullptr);
    EXPECT_EQ(config.upstream_operation_, nullptr);
  }

  {
    envoy::config::core::v3::TrafficDirection traffic_direction =
        envoy::config::core::v3::TrafficDirection::OUTBOUND;
    ConnectionManagerTracingConfigProto tracing_config;
    tracing_config.set_verbose(true);

    auto* custom_tag = tracing_config.add_custom_tags();
    custom_tag->set_tag("foo");
    custom_tag->mutable_literal()->set_value("bar");
    auto* custom_tag2 = tracing_config.add_custom_tags();
    custom_tag2->set_tag("dynamic_foo");
    custom_tag2->set_value("%REQ(X-FOO)%");
    tracing_config.set_operation("%REQ(my-custom-downstream-operation)%");
    tracing_config.set_upstream_operation("my-custom-fixed-upstream-operation");

    ConnectionManagerTracingConfig config(traffic_direction, tracing_config);

    EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());
    EXPECT_EQ(true, config.verbose());
    EXPECT_EQ(256, config.maxPathTagLength());
    EXPECT_EQ(2, config.getCustomTags().size());

    EXPECT_EQ(100, config.getClientSampling().numerator());
    EXPECT_EQ(10000, config.getRandomSampling().numerator());
    EXPECT_EQ(10000, config.getOverallSampling().numerator());

    EXPECT_NE(config.operation_, nullptr);
    EXPECT_NE(config.upstream_operation_, nullptr);

    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    Formatter::Context formatter_context;
    Http::TestRequestHeaderMapImpl headers{{"my-custom-downstream-operation", "downstream_op"}};
    formatter_context.setRequestHeaders(headers);
    EXPECT_EQ("downstream_op", config.operation_->format(formatter_context, stream_info));
    EXPECT_EQ("my-custom-fixed-upstream-operation",
              config.upstream_operation_->format(formatter_context, stream_info));
  }
}

} // namespace
} // namespace Tracing
} // namespace Envoy
