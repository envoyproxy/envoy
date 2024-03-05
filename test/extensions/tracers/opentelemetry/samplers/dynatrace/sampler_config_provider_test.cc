#include <memory>
#include <string>
#include <utility>

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h"

#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/tracing/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class SamplerConfigProviderTest : public testing::Test {
public:
protected:
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> tracer_factory_context_;
};

TEST_F(SamplerConfigProviderTest, TestValueConfigured) {
  const std::string yaml_string = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
          token: "tokenval"
          http_uri:
            cluster: "cluster_name"
            uri: "https://testhost.com/otlp/v1/traces"
            timeout: 0.250s
          root_spans_per_minute: 3456

    )EOF";

  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config);
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(), 3456);
}

TEST_F(SamplerConfigProviderTest, TestNoValueConfigured) {
  const std::string yaml_string = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
          token: "tokenval"
          http_uri:
            cluster: "cluster_name"
            uri: "https://testhost.com/otlp/v1/traces"
            timeout: 0.250s

    )EOF";

  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config);
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(),
            SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
}

TEST_F(SamplerConfigProviderTest, TestValueZeroConfigured) {
  const std::string yaml_string = R"EOF(
          tenant: "abc12345"
          cluster_id: -1743916452
          token: "tokenval"
          http_uri:
            cluster: "cluster_name"
            uri: "https://testhost.com/otlp/v1/traces"
            timeout: 0.250s
          root_spans_per_minute: 0
    )EOF";

  envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  SamplerConfigProviderImpl config_provider(tracer_factory_context_, proto_config);
  EXPECT_EQ(config_provider.getSamplerConfig().getRootSpansPerMinute(),
            SamplerConfig::ROOT_SPANS_PER_MINUTE_DEFAULT);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
