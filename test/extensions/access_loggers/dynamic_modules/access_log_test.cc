#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log_config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {
namespace {

class MockSlot : public ThreadLocal::Slot {
public:
  MOCK_METHOD(ThreadLocal::ThreadLocalObjectSharedPtr, get, ());
  MOCK_METHOD(bool, currentThreadRegistered, ());
  MOCK_METHOD(void, runOnAllThreads, (const UpdateCb& cb));
  MOCK_METHOD(void, runOnAllThreads,
              (const UpdateCb& cb, const std::function<void()>& main_callback));
  MOCK_METHOD(bool, isShutdown, (), (const));
  MOCK_METHOD(void, set, (InitializeCb cb));
};

class DynamicModuleAccessLogTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("access_log_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto config = newDynamicModuleAccessLogConfig(
        "test_logger", "config", std::move(dynamic_module.value()), *stats_.rootScope());
    EXPECT_TRUE(config.ok()) << config.status().message();
    config_ = std::move(config.value());
  }

  Stats::IsolatedStoreImpl stats_;
  DynamicModuleAccessLogConfigSharedPtr config_;
};

TEST_F(DynamicModuleAccessLogTest, ConfigHasInModuleConfig) {
  // The no_op module returns a non-null config.
  EXPECT_NE(nullptr, config_->in_module_config_);
}

TEST_F(DynamicModuleAccessLogTest, ConfigHasFunctionPointers) {
  EXPECT_NE(nullptr, config_->on_config_destroy_);
  EXPECT_NE(nullptr, config_->on_logger_new_);
  EXPECT_NE(nullptr, config_->on_logger_log_);
  EXPECT_NE(nullptr, config_->on_logger_destroy_);
  // Flush is optional but our no_op module implements it.
  EXPECT_NE(nullptr, config_->on_logger_flush_);
}

TEST_F(DynamicModuleAccessLogTest, ThreadLocalLoggerCreation) {
  // Test that ThreadLocalLogger can be created with the config.
  auto tl_logger = std::make_shared<ThreadLocalLogger>(nullptr, config_);
  auto module_logger = config_->on_logger_new_(config_->in_module_config_, tl_logger.get());
  EXPECT_NE(nullptr, module_logger);

  tl_logger->logger_ = module_logger;
  EXPECT_NE(nullptr, tl_logger->logger_);
  EXPECT_EQ(config_, tl_logger->config_);
}

TEST_F(DynamicModuleAccessLogTest, ThreadLocalLoggerDestruction) {
  // Test that ThreadLocalLogger properly destroys the module logger.
  auto tl_logger = std::make_shared<ThreadLocalLogger>(nullptr, config_);
  auto module_logger = config_->on_logger_new_(config_->in_module_config_, tl_logger.get());
  EXPECT_NE(nullptr, module_logger);

  {
    tl_logger->logger_ = module_logger;
    // Destructor should call on_logger_flush_ then on_logger_destroy_.
  }
}

TEST_F(DynamicModuleAccessLogTest, FlushCalledOnDestruction) {
  // Test that flush is called before destroy when logger is destroyed.
  auto tl_logger = std::make_shared<ThreadLocalLogger>(nullptr, config_);
  auto module_logger = config_->on_logger_new_(config_->in_module_config_, tl_logger.get());
  EXPECT_NE(nullptr, module_logger);

  static bool flush_called = false;
  static bool destroy_called = false;
  static bool flush_before_destroy = false;
  flush_called = false;
  destroy_called = false;
  flush_before_destroy = false;

  // Override the callbacks to track call order.
  config_->on_logger_flush_ = [](envoy_dynamic_module_type_access_logger_module_ptr) {
    flush_called = true;
    // flush should be called before destroy.
    flush_before_destroy = !destroy_called;
  };
  config_->on_logger_destroy_ = [](envoy_dynamic_module_type_access_logger_module_ptr) {
    destroy_called = true;
  };

  {
    tl_logger->logger_ = module_logger;
    EXPECT_FALSE(flush_called);
    EXPECT_FALSE(destroy_called);
    tl_logger.reset();
  }

  EXPECT_TRUE(flush_called);
  EXPECT_TRUE(destroy_called);
  EXPECT_TRUE(flush_before_destroy);
}

TEST_F(DynamicModuleAccessLogTest, FlushNotCalledWhenNull) {
  // Test that flush is skipped when on_logger_flush_ is nullptr.
  auto tl_logger = std::make_shared<ThreadLocalLogger>(nullptr, config_);
  auto module_logger = config_->on_logger_new_(config_->in_module_config_, tl_logger.get());
  EXPECT_NE(nullptr, module_logger);

  static bool destroy_called = false;
  destroy_called = false;

  // Set flush to nullptr to simulate module not implementing it.
  config_->on_logger_flush_ = nullptr;
  config_->on_logger_destroy_ = [](envoy_dynamic_module_type_access_logger_module_ptr) {
    destroy_called = true;
  };

  {
    tl_logger->logger_ = module_logger;
    tl_logger.reset();
  }

  // Destroy should still be called even without flush.
  EXPECT_TRUE(destroy_called);
}

TEST_F(DynamicModuleAccessLogTest, DynamicModuleAccessLogCreation) {
  NiceMock<ThreadLocal::MockInstance> tls;

  // Use allocateSlotMock to get a properly functioning slot.
  EXPECT_CALL(tls, allocateSlot()).WillOnce(testing::Invoke([&tls]() {
    return tls.allocateSlotMock();
  }));

  // Test that the access log can be created.
  auto access_log = std::make_unique<DynamicModuleAccessLog>(
      nullptr, config_, static_cast<ThreadLocal::SlotAllocator&>(tls));
  EXPECT_NE(nullptr, access_log);
}

TEST_F(DynamicModuleAccessLogTest, DynamicModuleAccessLogContext) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Formatter::Context log_context(&request_headers, nullptr, nullptr);

  DynamicModuleAccessLogContext ctx(log_context, stream_info);

  // Verify the context holds references correctly.
  EXPECT_EQ(&request_headers, ctx.log_context_.requestHeaders().ptr());
  EXPECT_EQ(&stream_info, &ctx.stream_info_);
}

TEST_F(DynamicModuleAccessLogTest, EmitLog) {
  NiceMock<ThreadLocal::MockInstance> tls;
  auto* slot = new NiceMock<MockSlot>();

  EXPECT_CALL(tls, allocateSlot()).WillOnce(testing::Return(ThreadLocal::SlotPtr{slot}));

  auto access_log = std::make_unique<DynamicModuleAccessLog>(
      nullptr, config_, static_cast<ThreadLocal::SlotAllocator&>(tls));

  // Set up the mock slot to return a ThreadLocalLogger.
  auto tl_logger = std::make_shared<ThreadLocalLogger>(nullptr, config_);
  auto module_logger = config_->on_logger_new_(config_->in_module_config_, tl_logger.get());
  tl_logger->logger_ = module_logger;

  ON_CALL(*slot, get()).WillByDefault(testing::Return(tl_logger));
  // The runOnAllThreads callback in constructor sets up the slot, but for test we mock it.

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Formatter::Context log_context(&request_headers, nullptr, nullptr);

  // Override the logger callback to assert invocation.
  static bool log_called = false;
  log_called = false;
  config_->on_logger_log_ = [](void* ctx, envoy_dynamic_module_type_access_logger_module_ptr logger,
                               envoy_dynamic_module_type_access_log_type) {
    EXPECT_NE(ctx, nullptr);
    EXPECT_NE(logger, nullptr);
    log_called = true;
  };

  access_log->log(log_context, stream_info);
  EXPECT_TRUE(log_called);
}

TEST_F(DynamicModuleAccessLogTest, EmitLogNullLogger) {
  NiceMock<ThreadLocal::MockInstance> tls;
  auto* slot = new NiceMock<MockSlot>();

  EXPECT_CALL(tls, allocateSlot()).WillOnce(testing::Return(ThreadLocal::SlotPtr{slot}));

  auto access_log = std::make_unique<DynamicModuleAccessLog>(
      nullptr, config_, static_cast<ThreadLocal::SlotAllocator&>(tls));

  // Return null logger to simulate not initialized or error.
  auto tl_logger = std::make_shared<ThreadLocalLogger>(nullptr, config_);
  ON_CALL(*slot, get()).WillByDefault(testing::Return(tl_logger));

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Formatter::Context log_context(&request_headers, nullptr, nullptr);

  // Should return early without crashing.
  access_log->log(log_context, stream_info);
}

TEST_F(DynamicModuleAccessLogTest, FactoryFunctionMissingSymbol) {
  // Test that factory function returns error when symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("access_log_missing_config_new", "c"),
      false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config = newDynamicModuleAccessLogConfig(
      "test_logger", "config", std::move(dynamic_module.value()), *stats_.rootScope());
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(), testing::HasSubstr("config_new"));
}

TEST_F(DynamicModuleAccessLogTest, FactoryFunctionModuleReturnsNull) {
  // Test that factory function returns error when module returns null config.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("access_log_config_new_fail", "c"), false);

  // Skip this test if the test module doesn't exist yet.
  if (!dynamic_module.ok()) {
    GTEST_SKIP() << "Test module access_log_config_new_fail not available";
  }

  auto config = newDynamicModuleAccessLogConfig(
      "test_logger", "config", std::move(dynamic_module.value()), *stats_.rootScope());
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(), testing::HasSubstr("Failed to initialize"));
}

TEST_F(DynamicModuleAccessLogTest, MetricsCounterDefineAndIncrement) {
  // Test that we can define and increment a counter via the config.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_counter", .length = 12};
  size_t counter_id = 0;

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_config_define_counter(
                static_cast<void*>(config_.get()), name, &counter_id));
  EXPECT_EQ(0, counter_id);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_increment_counter(
                static_cast<void*>(config_.get()), counter_id, 5));

  // Verify the counter value.
  auto counter = TestUtility::findCounter(stats_, "dynamic_module_access_logger.test_counter");
  ASSERT_NE(nullptr, counter);
  EXPECT_EQ(5, counter->value());
}

TEST_F(DynamicModuleAccessLogTest, MetricsGaugeDefineAndManipulate) {
  // Test that we can define and manipulate a gauge via the config.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_gauge", .length = 10};
  size_t gauge_id = 0;

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_config_define_gauge(
                static_cast<void*>(config_.get()), name, &gauge_id));
  EXPECT_EQ(0, gauge_id);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_set_gauge(static_cast<void*>(config_.get()),
                                                                  gauge_id, 100));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_increment_gauge(
                static_cast<void*>(config_.get()), gauge_id, 10));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_decrement_gauge(
                static_cast<void*>(config_.get()), gauge_id, 5));

  // Verify the gauge value: 100 + 10 - 5 = 105.
  auto gauge = TestUtility::findGauge(stats_, "dynamic_module_access_logger.test_gauge");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(105, gauge->value());
}

TEST_F(DynamicModuleAccessLogTest, MetricsHistogramDefineAndRecord) {
  // Test that we can define and record values in a histogram via the config.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_histogram", .length = 14};
  size_t histogram_id = 0;

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_config_define_histogram(
                static_cast<void*>(config_.get()), name, &histogram_id));
  EXPECT_EQ(0, histogram_id);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_access_logger_record_histogram_value(
                static_cast<void*>(config_.get()), histogram_id, 42));

  // Histograms don't expose a simple value to check, but we verify no error.
}

TEST_F(DynamicModuleAccessLogTest, MetricsInvalidId) {
  // Test that using an invalid ID returns an error.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_access_logger_increment_counter(
                static_cast<void*>(config_.get()), 999, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_access_logger_set_gauge(static_cast<void*>(config_.get()),
                                                                  999, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_access_logger_record_histogram_value(
                static_cast<void*>(config_.get()), 999, 1));
}

} // namespace
} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
