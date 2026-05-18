#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/tracers/dynamic_modules/tracer_config.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicModules {
namespace {

void setTestModulesSearchPath() {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);
}

DynamicModuleTracerConfigSharedPtr createTracerConfig(const std::string& module_name,
                                                      Stats::Scope& scope) {
  auto module =
      Envoy::Extensions::DynamicModules::newDynamicModuleByName(module_name, false, false);
  EXPECT_TRUE(module.ok());
  auto config_or =
      newDynamicModuleTracerConfig("test_tracer", "", "test_ns", std::move(module.value()), scope);
  EXPECT_TRUE(config_or.ok());
  return config_or.value();
}

// =============================================================================
// DynamicModuleTracerConfig tests.
// =============================================================================

class TracerConfigTest : public ::testing::Test {
public:
  TracerConfigTest() { setTestModulesSearchPath(); }
};

TEST_F(TracerConfigTest, CreateSuccess) {
  auto module =
      Envoy::Extensions::DynamicModules::newDynamicModuleByName("tracer_no_op", false, false);
  ASSERT_TRUE(module.ok());

  Stats::IsolatedStoreImpl store;
  auto config = newDynamicModuleTracerConfig("test_tracer", "test_config", "test_ns",
                                             std::move(module.value()), *store.rootScope());
  ASSERT_TRUE(config.ok());
  EXPECT_NE(config.value()->in_module_config_, nullptr);
}

TEST_F(TracerConfigTest, CreateFailMissingSymbol) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModuleByName("no_op", false, false);
  ASSERT_TRUE(module.ok());

  Stats::IsolatedStoreImpl store;
  auto config = newDynamicModuleTracerConfig("test_tracer", "", "test_ns",
                                             std::move(module.value()), *store.rootScope());
  ASSERT_FALSE(config.ok());
}

TEST_F(TracerConfigTest, CreateFailConfigInitReturnsNull) {
  auto module =
      Envoy::Extensions::DynamicModules::newDynamicModuleByName("tracer_config_fail", false, false);
  ASSERT_TRUE(module.ok());

  Stats::IsolatedStoreImpl store;
  auto config = newDynamicModuleTracerConfig("test_tracer", "", "test_ns",
                                             std::move(module.value()), *store.rootScope());
  ASSERT_FALSE(config.ok());
  EXPECT_EQ(config.status().message(), "Failed to initialize dynamic module tracer config");
}

// =============================================================================
// DynamicModuleDriver tests.
// =============================================================================

class DriverTest : public ::testing::Test {
public:
  DriverTest() {
    setTestModulesSearchPath();
    config_ = createTracerConfig("tracer_no_op", *store_.rootScope());
    driver_ = std::make_shared<DynamicModuleDriver>(config_);
  }

  Stats::IsolatedStoreImpl store_;
  DynamicModuleTracerConfigSharedPtr config_;
  std::shared_ptr<DynamicModuleDriver> driver_;
  NiceMock<Tracing::MockConfig> tracing_config_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(DriverTest, StartSpanSuccess) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  EXPECT_NE(span, nullptr);
}

TEST_F(DriverTest, SpanSetOperation) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  span->setOperation("new_operation");
}

TEST_F(DriverTest, SpanSetTag) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  span->setTag("component", "proxy");
}

TEST_F(DriverTest, SpanLog) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  span->log(SystemTime{}, "test_event");
}

TEST_F(DriverTest, SpanFinish) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  span->finishSpan();
}

TEST_F(DriverTest, SpanInjectContext) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  Tracing::TestTraceContextImpl outgoing_context{};
  span->injectContext(outgoing_context, Tracing::UpstreamContext{});
}

TEST_F(DriverTest, SpanSpawnChild) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto child = span->spawnChild(tracing_config_, "child_operation", SystemTime{});
  EXPECT_NE(child, nullptr);
}

TEST_F(DriverTest, SpanSetSampled) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  span->setSampled(false);
}

TEST_F(DriverTest, SpanUseLocalDecision) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  EXPECT_TRUE(span->useLocalDecision());
}

TEST_F(DriverTest, SpanBaggage) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  span->setBaggage("key", "value");
  auto baggage = span->getBaggage("key");
  EXPECT_TRUE(baggage.empty());
}

TEST_F(DriverTest, SpanGetTraceId) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto trace_id = span->getTraceId();
  EXPECT_TRUE(trace_id.empty());
}

TEST_F(DriverTest, SpanGetSpanId) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto span_id = span->getSpanId();
  EXPECT_TRUE(span_id.empty());
}

// =============================================================================
// Tests using the tracer_with_values module for edge case coverage.
// =============================================================================

class DriverWithValuesTest : public ::testing::Test {
public:
  DriverWithValuesTest() {
    setTestModulesSearchPath();
    config_ = createTracerConfig("tracer_with_values", *store_.rootScope());
    driver_ = std::make_shared<DynamicModuleDriver>(config_);
  }

  Stats::IsolatedStoreImpl store_;
  DynamicModuleTracerConfigSharedPtr config_;
  std::shared_ptr<DynamicModuleDriver> driver_;
  NiceMock<Tracing::MockConfig> tracing_config_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(DriverWithValuesTest, StartSpanReturnsNullSpan) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "null_span", decision);
  EXPECT_NE(span, nullptr);
  // The returned span should be a NullSpan since the module returned nullptr.
  span->finishSpan();
}

TEST_F(DriverWithValuesTest, SpawnChildReturnsNullSpan) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto child = span->spawnChild(tracing_config_, "child_operation", SystemTime{});
  EXPECT_NE(child, nullptr);
  // The returned child should be a NullSpan since the module returned nullptr.
  child->finishSpan();
}

TEST_F(DriverWithValuesTest, GetBaggageReturnsValue) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto baggage = span->getBaggage("test_key");
  EXPECT_EQ(baggage, "test_baggage_value");
}

TEST_F(DriverWithValuesTest, GetBaggageReturnsEmptyForUnknownKey) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto baggage = span->getBaggage("unknown_key");
  EXPECT_TRUE(baggage.empty());
}

TEST_F(DriverWithValuesTest, GetTraceIdReturnsValue) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto trace_id = span->getTraceId();
  EXPECT_EQ(trace_id, "abc123trace");
}

TEST_F(DriverWithValuesTest, GetSpanIdReturnsValue) {
  Tracing::TestTraceContextImpl trace_context{};
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation", decision);
  auto span_id = span->getSpanId();
  EXPECT_EQ(span_id, "def456span");
}

TEST_F(DriverTest, AllTraceReasons) {
  Tracing::TestTraceContextImpl trace_context{};

  const std::vector<Tracing::Reason> reasons = {
      Tracing::Reason::NotTraceable, Tracing::Reason::HealthCheck, Tracing::Reason::Sampling,
      Tracing::Reason::ServiceForced, Tracing::Reason::ClientForced};

  for (auto reason : reasons) {
    Tracing::Decision decision{reason, true};
    auto span = driver_->startSpan(tracing_config_, trace_context, stream_info_, "test_operation",
                                   decision);
    EXPECT_NE(span, nullptr);
  }
}

} // namespace
} // namespace DynamicModules
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
