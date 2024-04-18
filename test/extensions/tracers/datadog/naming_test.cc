/**
 * The tests in this file aren't specific to a class, but instead test all
 * behavior related to spans' "operation name" (a.k.a. "span name"),
 * "resource name," and "service name."
 *
 * Datadog's model of a span is different from Envoy's. Each Datadog span,
 * in addition to having a "service name" and an "operation name," also has a
 * "resource name." The operation name indicates the _kind_ of operation
 * that is being performed by the service, whereas the resource name contains
 * more specifics about what is being operated upon, or about what is doing the
 * operating. Envoy has no notion of "resource name," and instead uses
 * operation name and tags for this purpose.
 *
 * When Envoy's tracing interface indicates an operation name, the Datadog
 * tracer translates it into a resource name instead. The actual Datadog
 * operation name is always hard-coded to the value "envoy.proxy".
 *
 * Finally, each span's "service name" is derived either from the tracer's
 * configuration or a hard-coded default, which is "envoy".
 *
 * The tests in this file verify all of this behavior for a variety of
 * scenarios where spans are created or modified.
 */

#include "source/extensions/tracers/datadog/config.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/tracer.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

template <typename Config> Config makeConfig(const std::string& yaml) {
  Config result;
  TestUtility::loadFromYaml(yaml, result);
  return result;
}

class DatadogTracerNamingTest : public testing::Test {
public:
  DatadogTracerNamingTest();

protected:
  /**
   * Verify that a tracer configured using the specified \p config_yaml
   * produces spans and child spans having the specified
   * \p expected_service_name.
   * @param config_yaml YAML representation of a
   *    \c type.googleapis.com/envoy.config.trace.v3.DatadogConfig
   * @param expected_service_name service name to expect each span to have
   */
  void serviceNameTest(const std::string& config_yaml, const std::string& expected_service_name);

  /**
   * Assign through the specified \p result a pointer to the underlying
   * \c datadog::tracing::Span to which the specified \p span refers.
   * If \p span does not refer to a Datadog span, then this function triggers a
   * fatal test assertion.
   * An output parameter is used because the \c ASSERT_* macros require that
   * the enclosing function have \c void return type.
   * @param result pointer to the output value to overwrite
   * @param span pointer to an Envoy span that refers to a Datadog span
   */
  static void asDatadogSpan(const datadog::tracing::Span** result, const Tracing::SpanPtr& span);

  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::TestUtil::TestStore store_;
  NiceMock<ThreadLocal::MockInstance> thread_local_slot_allocator_;
  Event::SimulatedTimeSystem time_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

DatadogTracerNamingTest::DatadogTracerNamingTest() {
  cluster_manager_.initializeClusters({"fake_cluster"}, {});
  cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
  cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
}

void DatadogTracerNamingTest::asDatadogSpan(const datadog::tracing::Span** result,
                                            const Tracing::SpanPtr& span) {
  ASSERT_TRUE(span);
  const auto as_dd_span_wrapper = dynamic_cast<Span*>(span.get());
  ASSERT_NE(nullptr, as_dd_span_wrapper);

  const datadog::tracing::Optional<datadog::tracing::Span>& maybe_dd_span =
      as_dd_span_wrapper->impl();
  ASSERT_TRUE(maybe_dd_span);
  *result = &*maybe_dd_span;
}

void DatadogTracerNamingTest::serviceNameTest(const std::string& config_yaml,
                                              const std::string& expected_service_name) {
  auto config_proto = makeConfig<envoy::config::trace::v3::DatadogConfig>(config_yaml);

  Tracer tracer{config_proto.collector_cluster(),
                config_proto.collector_hostname(),
                DatadogTracerFactory::makeConfig(config_proto),
                cluster_manager_,
                *store_.rootScope(),
                thread_local_slot_allocator_,
                time_};

  // Any values will do for the sake of this test. What we care about is the
  // `expected_service_name`.
  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = true;
  const std::string operation_name = "some.operation.name";
  Tracing::TestTraceContextImpl context{};

  const Tracing::SpanPtr span =
      tracer.startSpan(Tracing::MockConfig{}, context, stream_info_, operation_name, decision);
  const datadog::tracing::Span* dd_span;
  asDatadogSpan(&dd_span, span);

  EXPECT_EQ(expected_service_name, dd_span->service_name());

  const auto child_start = time_.timeSystem().systemTime();
  const std::string child_operation_name = "some.other.operation.name";
  const Tracing::SpanPtr child =
      span->spawnChild(Tracing::MockConfig{}, child_operation_name, child_start);
  const datadog::tracing::Span* dd_child;
  asDatadogSpan(&dd_child, child);

  EXPECT_EQ(expected_service_name, dd_child->service_name());
}

TEST_F(DatadogTracerNamingTest, ServiceNameConfigured) {
  // If you specify a `service_name` in the tracer configuration, then spans
  // created will have that service name.
  serviceNameTest(R"EOF(
    collector_cluster: fake_cluster
    service_name: mr_bigglesworth
   )EOF",
                  "mr_bigglesworth");
}

TEST_F(DatadogTracerNamingTest, ServiceNameDefault) {
  // If you don't specify a `service_name` in the tracer configuration, then
  // spans created will have the default service name, which is "envoy".
  serviceNameTest(R"EOF(
    collector_cluster: fake_cluster
   )EOF",
                  "envoy");
}

TEST_F(DatadogTracerNamingTest, OperationNameAndResourceName) {
  // Concerns:
  //
  // - The span returned by `Tracer::startSpan` has as its resource name the
  //   operation name passed to `Tracer::startSpan`, and has as its operation
  //   name "envoy.proxy".
  // - The span returned by `Span::spawnChild` has as its resource name the
  //   operation name passed to `Tracer::spawnChild`, and has as its operation
  //   name "envoy.proxy".
  // - `Span::setOperation` sets the resource name of the span, but does not
  //   change the operation name.

  auto config_proto = makeConfig<envoy::config::trace::v3::DatadogConfig>(R"EOF(
    collector_cluster: fake_cluster
   )EOF");

  Tracer tracer{config_proto.collector_cluster(),
                config_proto.collector_hostname(),
                DatadogTracerFactory::makeConfig(config_proto),
                cluster_manager_,
                *store_.rootScope(),
                thread_local_slot_allocator_,
                time_};

  // Any values will do for the sake of this test. What we care about are the
  // operation names and the resource names.
  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = true;
  Tracing::TestTraceContextImpl context{};

  const std::string operation_name = "some.operation.name";
  const Tracing::SpanPtr span =
      tracer.startSpan(Tracing::MockConfig{}, context, stream_info_, operation_name, decision);
  const datadog::tracing::Span* dd_span;
  asDatadogSpan(&dd_span, span);

  EXPECT_EQ("envoy.proxy", dd_span->name());
  EXPECT_EQ(operation_name, dd_span->resource_name());

  const std::string new_operation_name = "some.new.operation.name";
  span->setOperation(new_operation_name);

  EXPECT_EQ("envoy.proxy", dd_span->name());
  EXPECT_EQ(new_operation_name, dd_span->resource_name());

  const auto child_start = time_.timeSystem().systemTime();
  const std::string child_operation_name = "some.child.operation.name";
  const Tracing::SpanPtr child =
      span->spawnChild(Tracing::MockConfig{}, child_operation_name, child_start);
  const datadog::tracing::Span* dd_child;
  asDatadogSpan(&dd_child, child);

  EXPECT_EQ("envoy.proxy", dd_child->name());
  EXPECT_EQ(child_operation_name, dd_child->resource_name());

  const std::string child_new_operation_name = "some.child.new.operation.name";
  child->setOperation(child_new_operation_name);

  EXPECT_EQ("envoy.proxy", dd_child->name());
  EXPECT_EQ(child_new_operation_name, dd_child->resource_name());
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
