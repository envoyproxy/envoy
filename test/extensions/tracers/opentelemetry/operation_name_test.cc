#include "source/extensions/tracers/opentelemetry/config.h"
#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/tracer.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class OpenTelemetryTracerOperationNameTest : public testing::Test {
public:
  OpenTelemetryTracerOperationNameTest();

protected:
  NiceMock<Tracing::MockConfig> config;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
};

OpenTelemetryTracerOperationNameTest::OpenTelemetryTracerOperationNameTest() {
  cluster_manager_.initializeClusters({"fake-cluster"}, {});
  cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "fake-cluster";
  cluster_manager_.initializeThreadLocalClusters({"fake-cluster"});
}

TEST_F(OpenTelemetryTracerOperationNameTest, OperationName) {
  // Checks:
  //
  // - The span returned by `Tracer::startSpan` has as its name the
  //   operation name passed to `Tracer::startSpan`
  // - `Span::setOperation` sets the name of the span

  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: test-service-name
    )EOF";

  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  auto driver = std::make_unique<Driver>(opentelemetry_config, context);

  const std::string operation_name = "initial_operation_name";
  Tracing::SpanPtr tracing_span = driver->startSpan(
      config, trace_context, stream_info, operation_name, {Tracing::Reason::Sampling, true});

  EXPECT_EQ(dynamic_cast<Span*>(tracing_span.get())->name(), operation_name);

  const std::string new_operation_name = "the_new_operation_name";
  tracing_span->setOperation(new_operation_name);
  EXPECT_EQ(dynamic_cast<Span*>(tracing_span.get())->name(), new_operation_name);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
