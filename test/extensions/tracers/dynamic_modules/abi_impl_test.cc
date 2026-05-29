#include <atomic>
#include <thread>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/tracers/dynamic_modules/tracer_config.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

class AbiImplTest : public ::testing::Test {
public:
  AbiImplTest() {
    setTestModulesSearchPath();
    auto module =
        Envoy::Extensions::DynamicModules::newDynamicModuleByName("tracer_no_op", false, false);
    EXPECT_TRUE(module.ok());
    auto config_or = newDynamicModuleTracerConfig("test_tracer", "", "test_ns",
                                                  std::move(module.value()), *store_.rootScope());
    EXPECT_TRUE(config_or.ok());
    config_ = config_or.value();
    // Re-open stat creation so tests can call `define_*` from the test thread.
    config_->stat_creation_frozen_ = false;
    driver_ = std::make_shared<DynamicModuleDriver>(config_);
  }

  Tracing::SpanPtr createSpan() {
    Tracing::TestTraceContextImpl trace_context{};
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    NiceMock<Tracing::MockConfig> tracing_config;
    Tracing::Decision decision{Tracing::Reason::Sampling, true};
    return driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation",
                              decision);
  }

  Stats::IsolatedStoreImpl store_;
  DynamicModuleTracerConfigSharedPtr config_;
  std::shared_ptr<DynamicModuleDriver> driver_;
};

// =============================================================================
// Null trace context tests. These exercise the defensive ctx == nullptr branches.
// =============================================================================

TEST_F(AbiImplTest, GetTraceContextValueNullContext) {
  DynamicModuleSpan span(
      config_, reinterpret_cast<envoy_dynamic_module_type_tracer_span_module_ptr>(uintptr_t(1)),
      nullptr);
  envoy_dynamic_module_type_module_buffer key = {.ptr = "traceparent", .length = 11};
  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_value(
      static_cast<void*>(&span), key, &value_out));
  EXPECT_EQ(value_out.ptr, nullptr);
}

TEST_F(AbiImplTest, SetTraceContextValueNullContext) {
  DynamicModuleSpan span(
      config_, reinterpret_cast<envoy_dynamic_module_type_tracer_span_module_ptr>(uintptr_t(1)),
      nullptr);
  envoy_dynamic_module_type_module_buffer key = {.ptr = "traceparent", .length = 11};
  envoy_dynamic_module_type_module_buffer value = {.ptr = "value", .length = 5};
  envoy_dynamic_module_callback_tracer_set_trace_context_value(static_cast<void*>(&span), key,
                                                               value);
}

TEST_F(AbiImplTest, RemoveTraceContextValueNullContext) {
  DynamicModuleSpan span(
      config_, reinterpret_cast<envoy_dynamic_module_type_tracer_span_module_ptr>(uintptr_t(1)),
      nullptr);
  envoy_dynamic_module_type_module_buffer key = {.ptr = "traceparent", .length = 11};
  envoy_dynamic_module_callback_tracer_remove_trace_context_value(static_cast<void*>(&span), key);
}

TEST_F(AbiImplTest, GetTraceContextProtocolNullContext) {
  DynamicModuleSpan span(
      config_, reinterpret_cast<envoy_dynamic_module_type_tracer_span_module_ptr>(uintptr_t(1)),
      nullptr);
  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_protocol(
      static_cast<void*>(&span), &value_out));
  EXPECT_EQ(value_out.ptr, nullptr);
}

TEST_F(AbiImplTest, GetTraceContextHostNullContext) {
  DynamicModuleSpan span(
      config_, reinterpret_cast<envoy_dynamic_module_type_tracer_span_module_ptr>(uintptr_t(1)),
      nullptr);
  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_host(
      static_cast<void*>(&span), &value_out));
  EXPECT_EQ(value_out.ptr, nullptr);
}

TEST_F(AbiImplTest, GetTraceContextPathNullContext) {
  DynamicModuleSpan span(
      config_, reinterpret_cast<envoy_dynamic_module_type_tracer_span_module_ptr>(uintptr_t(1)),
      nullptr);
  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_path(
      static_cast<void*>(&span), &value_out));
  EXPECT_EQ(value_out.ptr, nullptr);
}

TEST_F(AbiImplTest, GetTraceContextMethodNullContext) {
  DynamicModuleSpan span(
      config_, reinterpret_cast<envoy_dynamic_module_type_tracer_span_module_ptr>(uintptr_t(1)),
      nullptr);
  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_method(
      static_cast<void*>(&span), &value_out));
  EXPECT_EQ(value_out.ptr, nullptr);
}

// =============================================================================
// Empty value tests for host, path, and method.
// =============================================================================

TEST_F(AbiImplTest, GetTraceContextHostEmpty) {
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_host(
      static_cast<void*>(dyn_span), &value_out));
}

TEST_F(AbiImplTest, GetTraceContextPathEmpty) {
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_path(
      static_cast<void*>(dyn_span), &value_out));
}

TEST_F(AbiImplTest, GetTraceContextMethodEmpty) {
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_method(
      static_cast<void*>(dyn_span), &value_out));
}

// =============================================================================
// Trace context value tests with non-null context.
// =============================================================================

TEST_F(AbiImplTest, GetTraceContextValue) {
  Tracing::TestTraceContextImpl trace_context{{"traceparent", "test-value"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_module_buffer key = {.ptr = "traceparent", .length = 11};
  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_tracer_get_trace_context_value(
      static_cast<void*>(dyn_span), key, &value_out));
  EXPECT_EQ(std::string(value_out.ptr, value_out.length), "test-value");
}

TEST_F(AbiImplTest, GetTraceContextValueNotFound) {
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_module_buffer key = {.ptr = "nonexistent", .length = 11};
  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_value(
      static_cast<void*>(dyn_span), key, &value_out));
}

TEST_F(AbiImplTest, SetTraceContextValue) {
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_module_buffer key = {.ptr = "traceparent", .length = 11};
  envoy_dynamic_module_type_module_buffer value = {.ptr = "new-value", .length = 9};
  envoy_dynamic_module_callback_tracer_set_trace_context_value(static_cast<void*>(dyn_span), key,
                                                               value);

  auto result = trace_context.get("traceparent");
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), "new-value");
}

TEST_F(AbiImplTest, RemoveTraceContextValue) {
  Tracing::TestTraceContextImpl trace_context{{"traceparent", "test-value"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_module_buffer key = {.ptr = "traceparent", .length = 11};
  envoy_dynamic_module_callback_tracer_remove_trace_context_value(static_cast<void*>(dyn_span),
                                                                  key);

  auto result = trace_context.get("traceparent");
  EXPECT_FALSE(result.has_value());
}

TEST_F(AbiImplTest, GetTraceContextProtocolEmpty) {
  Tracing::TestTraceContextImpl trace_context{};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_tracer_get_trace_context_protocol(
      static_cast<void*>(dyn_span), &value_out));
}

TEST_F(AbiImplTest, GetTraceContextProtocol) {
  Tracing::TestTraceContextImpl trace_context{{":protocol", "HTTP/1.1"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_tracer_get_trace_context_protocol(
      static_cast<void*>(dyn_span), &value_out));
  EXPECT_EQ(std::string(value_out.ptr, value_out.length), "HTTP/1.1");
}

TEST_F(AbiImplTest, GetTraceContextHost) {
  Tracing::TestTraceContextImpl trace_context{{":authority", "example.com"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_tracer_get_trace_context_host(
      static_cast<void*>(dyn_span), &value_out));
  EXPECT_EQ(std::string(value_out.ptr, value_out.length), "example.com");
}

TEST_F(AbiImplTest, GetTraceContextPath) {
  Tracing::TestTraceContextImpl trace_context{{":path", "/api/v1/test"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_tracer_get_trace_context_path(
      static_cast<void*>(dyn_span), &value_out));
  EXPECT_EQ(std::string(value_out.ptr, value_out.length), "/api/v1/test");
}

TEST_F(AbiImplTest, GetTraceContextMethod) {
  Tracing::TestTraceContextImpl trace_context{{":method", "POST"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Tracing::MockConfig> tracing_config;
  Tracing::Decision decision{Tracing::Reason::Sampling, true};

  auto span =
      driver_->startSpan(tracing_config, trace_context, stream_info, "test_operation", decision);
  auto* dyn_span = dynamic_cast<DynamicModuleSpan*>(span.get());
  ASSERT_NE(dyn_span, nullptr);

  envoy_dynamic_module_type_envoy_buffer value_out = {.ptr = nullptr, .length = 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_tracer_get_trace_context_method(
      static_cast<void*>(dyn_span), &value_out));
  EXPECT_EQ(std::string(value_out.ptr, value_out.length), "POST");
}

// =============================================================================
// Metrics Tests
// =============================================================================

TEST_F(AbiImplTest, DefineAndIncrementCounter) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_counter", .length = 12};
  size_t counter_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_counter(
      static_cast<void*>(config_.get()), name, nullptr, 0, &counter_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(counter_id, 0u);

  auto result = envoy_dynamic_module_callback_tracer_increment_counter(
      static_cast<void*>(config_.get()), counter_id, nullptr, 0, 5);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
}

TEST_F(AbiImplTest, IncrementCounterInvalidId) {
  auto result = envoy_dynamic_module_callback_tracer_increment_counter(
      static_cast<void*>(config_.get()), 999, nullptr, 0, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

TEST_F(AbiImplTest, DefineAndSetGauge) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_gauge", .length = 10};
  size_t gauge_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_gauge(
      static_cast<void*>(config_.get()), name, nullptr, 0, &gauge_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(gauge_id, 0u);

  auto result = envoy_dynamic_module_callback_tracer_set_gauge(static_cast<void*>(config_.get()),
                                                               gauge_id, nullptr, 0, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
}

TEST_F(AbiImplTest, SetGaugeInvalidId) {
  auto result = envoy_dynamic_module_callback_tracer_set_gauge(static_cast<void*>(config_.get()),
                                                               999, nullptr, 0, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

TEST_F(AbiImplTest, DefineAndRecordHistogram) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_histogram", .length = 14};
  size_t histogram_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_histogram(
      static_cast<void*>(config_.get()), name, nullptr, 0, &histogram_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(histogram_id, 0u);

  auto result = envoy_dynamic_module_callback_tracer_record_histogram_value(
      static_cast<void*>(config_.get()), histogram_id, nullptr, 0, 100);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
}

TEST_F(AbiImplTest, RecordHistogramInvalidId) {
  auto result = envoy_dynamic_module_callback_tracer_record_histogram_value(
      static_cast<void*>(config_.get()), 999, nullptr, 0, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

TEST_F(AbiImplTest, DefineAndIncrementCounterVec) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_counter_vec", .length = 16};
  envoy_dynamic_module_type_module_buffer label_names[] = {{.ptr = "method", .length = 6},
                                                           {.ptr = "status", .length = 6}};
  size_t counter_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_counter(
      static_cast<void*>(config_.get()), name, label_names, 2, &counter_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(counter_id, 0u);

  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "GET", .length = 3},
                                                            {.ptr = "200", .length = 3}};
  auto result = envoy_dynamic_module_callback_tracer_increment_counter(
      static_cast<void*>(config_.get()), counter_id, label_values, 2, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
}

TEST_F(AbiImplTest, IncrementCounterVecInvalidLabels) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_counter_vec2", .length = 17};
  envoy_dynamic_module_type_module_buffer label_names[] = {{.ptr = "method", .length = 6}};
  size_t counter_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_counter(
      static_cast<void*>(config_.get()), name, label_names, 1, &counter_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);

  // Wrong number of label values.
  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "GET", .length = 3},
                                                            {.ptr = "200", .length = 3}};
  auto result = envoy_dynamic_module_callback_tracer_increment_counter(
      static_cast<void*>(config_.get()), counter_id, label_values, 2, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

TEST_F(AbiImplTest, DefineAndSetGaugeVec) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_gauge_vec", .length = 14};
  envoy_dynamic_module_type_module_buffer label_names[] = {{.ptr = "host", .length = 4}};
  size_t gauge_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_gauge(
      static_cast<void*>(config_.get()), name, label_names, 1, &gauge_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(gauge_id, 0u);

  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "example.com", .length = 11}};
  auto result = envoy_dynamic_module_callback_tracer_set_gauge(static_cast<void*>(config_.get()),
                                                               gauge_id, label_values, 1, 99);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
}

TEST_F(AbiImplTest, DefineAndRecordHistogramVec) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_hist_vec", .length = 13};
  envoy_dynamic_module_type_module_buffer label_names[] = {{.ptr = "path", .length = 4}};
  size_t histogram_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_histogram(
      static_cast<void*>(config_.get()), name, label_names, 1, &histogram_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(histogram_id, 0u);

  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "/api", .length = 4}};
  auto result = envoy_dynamic_module_callback_tracer_record_histogram_value(
      static_cast<void*>(config_.get()), histogram_id, label_values, 1, 250);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
}

TEST_F(AbiImplTest, SetGaugeVecInvalidLabels) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_gauge_vec2", .length = 15};
  envoy_dynamic_module_type_module_buffer label_names[] = {{.ptr = "host", .length = 4}};
  size_t gauge_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_gauge(
      static_cast<void*>(config_.get()), name, label_names, 1, &gauge_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);

  // Wrong number of label values.
  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "a", .length = 1},
                                                            {.ptr = "b", .length = 1}};
  auto result = envoy_dynamic_module_callback_tracer_set_gauge(static_cast<void*>(config_.get()),
                                                               gauge_id, label_values, 2, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

TEST_F(AbiImplTest, RecordHistogramVecInvalidLabels) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_hist_vec2", .length = 14};
  envoy_dynamic_module_type_module_buffer label_names[] = {{.ptr = "path", .length = 4}};
  size_t histogram_id = 0;
  auto define_result = envoy_dynamic_module_callback_tracer_define_histogram(
      static_cast<void*>(config_.get()), name, label_names, 1, &histogram_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);

  // Wrong number of label values.
  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "a", .length = 1},
                                                            {.ptr = "b", .length = 1}};
  auto result = envoy_dynamic_module_callback_tracer_record_histogram_value(
      static_cast<void*>(config_.get()), histogram_id, label_values, 2, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

TEST_F(AbiImplTest, IncrementCounterVecNotFound) {
  // Try to use a non-existent vec ID.
  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "v", .length = 1}};
  auto result = envoy_dynamic_module_callback_tracer_increment_counter(
      static_cast<void*>(config_.get()), 999, label_values, 1, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

TEST_F(AbiImplTest, SetGaugeVecNotFound) {
  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "v", .length = 1}};
  auto result = envoy_dynamic_module_callback_tracer_set_gauge(static_cast<void*>(config_.get()),
                                                               999, label_values, 1, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

TEST_F(AbiImplTest, RecordHistogramVecNotFound) {
  envoy_dynamic_module_type_module_buffer label_values[] = {{.ptr = "v", .length = 1}};
  auto result = envoy_dynamic_module_callback_tracer_record_histogram_value(
      static_cast<void*>(config_.get()), 999, label_values, 1, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// Verifies the factory auto-freezes stat creation so `define_*` returns `Frozen` after init.
TEST_F(AbiImplTest, MetricsFrozenAfterInit) {
  config_->stat_creation_frozen_ = true;
  envoy_dynamic_module_type_module_buffer name = {.ptr = "frozen_counter", .length = 14};
  envoy_dynamic_module_type_module_buffer label_name = {.ptr = "label", .length = 5};
  size_t out_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_tracer_define_counter(static_cast<void*>(config_.get()),
                                                                name, nullptr, 0, &out_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_tracer_define_counter(static_cast<void*>(config_.get()),
                                                                name, &label_name, 1, &out_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_tracer_define_gauge(static_cast<void*>(config_.get()),
                                                              name, nullptr, 0, &out_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_tracer_define_histogram(static_cast<void*>(config_.get()),
                                                                  name, nullptr, 0, &out_id));
}

// Drives concurrent labeled increments from multiple threads to verify no data race in the
// shared `stat_name_pool_`. Run under `--config=tsan` to verify.
TEST_F(AbiImplTest, MetricsConcurrentIncrementCounterVecNoRace) {
  envoy_dynamic_module_type_module_buffer name = {.ptr = "race_counter", .length = 12};
  std::string label_name_str = "status";
  envoy_dynamic_module_type_module_buffer label_names[1] = {
      {.ptr = const_cast<char*>(label_name_str.data()), .length = label_name_str.size()}};
  size_t counter_id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_tracer_define_counter(static_cast<void*>(config_.get()),
                                                                name, label_names, 1, &counter_id));

  constexpr int kNumThreads = 8;
  constexpr int kIncrementsPerThread = 2000;

  // Pre-warm the test scope's counter cache so workers only hit the cache. `TestScope` uses an
  // unsynchronized map for counter caching that would otherwise race independently of the path
  // under test.
  for (int t = 0; t < kNumThreads; ++t) {
    const std::string label_value_str = absl::StrCat("worker_", t);
    envoy_dynamic_module_type_module_buffer label_value = {
        .ptr = const_cast<char*>(label_value_str.data()), .length = label_value_str.size()};
    ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
              envoy_dynamic_module_callback_tracer_increment_counter(
                  static_cast<void*>(config_.get()), counter_id, &label_value, 1, 0));
  }

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t]() {
      const std::string label_value_str = absl::StrCat("worker_", t);
      envoy_dynamic_module_type_module_buffer label_value = {
          .ptr = const_cast<char*>(label_value_str.data()), .length = label_value_str.size()};
      ready.fetch_add(1, std::memory_order_relaxed);
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIncrementsPerThread; ++i) {
        ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
                  envoy_dynamic_module_callback_tracer_increment_counter(
                      static_cast<void*>(config_.get()), counter_id, &label_value, 1, 1));
      }
    });
  }
  while (ready.load(std::memory_order_acquire) < kNumThreads) {
  }
  go.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }
}

} // namespace
} // namespace DynamicModules
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
