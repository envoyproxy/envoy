#include "source/extensions/bootstrap/dynamic_modules/extension.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class BootstrapAbiImplTest : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

// Test that the scheduler can be created, used, and deleted.
TEST_F(BootstrapAbiImplTest, SchedulerLifecycle) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
      config.value()->thisAsVoidPtr());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Delete the scheduler via the ABI callback.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);
}

// Test that the scheduler commit posts to the dispatcher.
TEST_F(BootstrapAbiImplTest, SchedulerCommit) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
      config.value()->thisAsVoidPtr());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Expect the dispatcher to receive a post call when commit is called.
  Event::PostCb captured_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  // Commit an event via the ABI callback.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(scheduler_ptr, 42);

  // Execute the callback to complete the flow.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);
}

// Test that onScheduled is called when the posted callback executes.
TEST_F(BootstrapAbiImplTest, OnScheduledCallback) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
      config.value()->thisAsVoidPtr());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Capture the posted callback.
  Event::PostCb captured_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  // Commit an event via the ABI callback.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(scheduler_ptr, 123);

  // Execute the captured callback to trigger onScheduled.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);
}

// Test that onScheduled handles the case when config is already destroyed.
TEST_F(BootstrapAbiImplTest, OnScheduledAfterConfigDestroyed) {
  Event::PostCb captured_cb;

  {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        testDataDir() + "/libbootstrap_no_op.so", false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

    auto config = newDynamicModuleBootstrapExtensionConfig("test", "config",
                                                           std::move(dynamic_module.value()),
                                                           dispatcher_, context_, context_.store_);
    ASSERT_TRUE(config.ok()) << config.status();

    // Create a scheduler via the ABI callback.
    auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
        config.value()->thisAsVoidPtr());
    EXPECT_NE(scheduler_ptr, nullptr);

    // Capture the posted callback.
    EXPECT_CALL(dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
      captured_cb = std::move(cb);
    }));

    // Commit an event via the ABI callback.
    envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(scheduler_ptr, 456);

    // Delete the scheduler before the callback is executed.
    envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);

    // Config goes out of scope here and is destroyed.
  }

  // Execute the captured callback after config is destroyed.
  // This should not crash - the weak_ptr should be expired.
  captured_cb();
}

// Test calling onScheduled directly.
TEST_F(BootstrapAbiImplTest, OnScheduledDirect) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Call onScheduled directly - this should call the in-module hook.
  config.value()->onScheduled(789);
}

// -----------------------------------------------------------------------------
// Init Manager Tests
// -----------------------------------------------------------------------------

// Test that an init target is automatically registered during config creation. The C no-op module
// calls signal_init_complete during its constructor. Calling it again here verifies idempotency.
TEST_F(BootstrapAbiImplTest, InitTargetAutoRegisteredAndSignal) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  // The init manager should receive an add call during config creation.
  EXPECT_CALL(context_.init_manager_, add(_));

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // The C no-op module already called signal_init_complete during config creation.
  // Calling it again verifies that duplicate calls are safe.
  envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
      config.value()->thisAsVoidPtr());
}

// -----------------------------------------------------------------------------
// HTTP Callout Tests
// -----------------------------------------------------------------------------

// Test HTTP callout returns ClusterNotFound when cluster does not exist.
TEST_F(BootstrapAbiImplTest, HttpCalloutClusterNotFound) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock to return nullptr for the cluster lookup.
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("nonexistent_cluster"))
      .WillOnce(testing::Return(nullptr));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"nonexistent_cluster", 19};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound);
}

// Test HTTP callout returns MissingRequiredHeaders when headers are missing.
TEST_F(BootstrapAbiImplTest, HttpCalloutMissingHeaders) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Headers missing :method, :path, and host.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {"x-custom", 8, "value", 5},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders);
}

// Test HTTP callout success path with response headers and body.
TEST_F(BootstrapAbiImplTest, HttpCalloutSuccess) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to capture the callback and return a request.
  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(callbacks_captured, nullptr);

  // Create a response with headers and body.
  Http::ResponseHeaderMapPtr resp_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-type", "text/plain"}}));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add("Hello, World!");

  // Trigger the onBeforeFinalizeUpstreamSpan callback to exercise the no-op override.
  Envoy::Tracing::MockSpan span;
  callbacks_captured->onBeforeFinalizeUpstreamSpan(span, nullptr);

  // Trigger the success callback.
  callbacks_captured->onSuccess(request, std::move(response));
}

// Test HTTP callout failure with Reset reason.
TEST_F(BootstrapAbiImplTest, HttpCalloutFailureReset) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to capture the callback and return a request.
  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(callbacks_captured, nullptr);

  // Trigger the failure callback with Reset reason.
  callbacks_captured->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

// Test HTTP callout failure with ExceedResponseBufferLimit reason.
TEST_F(BootstrapAbiImplTest, HttpCalloutFailureExceedBufferLimit) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to capture the callback and return a request.
  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(callbacks_captured, nullptr);

  // Trigger the failure callback with ExceedResponseBufferLimit reason.
  callbacks_captured->onFailure(request,
                                Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
}

// Test HTTP callout returns CannotCreateRequest when async client returns nullptr.
TEST_F(BootstrapAbiImplTest, HttpCalloutCannotCreateRequest) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to return nullptr (simulating request creation failure).
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Return(nullptr));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest);
}

// Test HTTP callout success when in_module_config is cleared before callback.
TEST_F(BootstrapAbiImplTest, HttpCalloutSuccessAfterInModuleConfigCleared) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to capture the callback and return a request.
  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(callbacks_captured, nullptr);

  // Clear the in_module_config to simulate the module being destroyed.
  config.value()->in_module_config_ = nullptr;

  // Create a response.
  Http::ResponseHeaderMapPtr resp_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));

  // Trigger the success callback. This should not crash and should just clean up.
  callbacks_captured->onSuccess(request, std::move(response));
}

// Test HTTP callout failure when in_module_config is cleared before callback.
TEST_F(BootstrapAbiImplTest, HttpCalloutFailureAfterInModuleConfigCleared) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to capture the callback and return a request.
  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(callbacks_captured, nullptr);

  // Clear the in_module_config to simulate the module being destroyed.
  config.value()->in_module_config_ = nullptr;

  // Trigger the failure callback. This should not crash and should just clean up.
  callbacks_captured->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

// Test HTTP callout with body in request.
TEST_F(BootstrapAbiImplTest, HttpCalloutWithBody) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to verify the body is passed correctly.
  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  std::string captured_body;
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            captured_body = message->body().toString();
            callbacks_captured = &callbacks;
            return &request;
          }));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "POST", 4},
      {":path", 5, "/api/v1", 7},
      {"host", 4, "api.example.com", 15},
      {"content-type", 12, "application/json", 16},
  };

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::string body_str = R"({"key": "value", "number": 42})";
  envoy_dynamic_module_type_module_buffer body = {body_str.data(),
                                                  static_cast<size_t>(body_str.size())};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 10000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_EQ(captured_body, body_str);
  EXPECT_NE(callbacks_captured, nullptr);

  // Complete the request to properly clean up.
  Http::ResponseHeaderMapPtr resp_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  callbacks_captured->onSuccess(request, std::move(response));
}

// -----------------------------------------------------------------------------
// Stats Access Tests
// -----------------------------------------------------------------------------

// Test get_counter_value callback with an existing counter.
TEST_F(BootstrapAbiImplTest, GetCounterValueExisting) {
  // Create a counter in the stats store.
  context_.store_.counterFromString("test.counter").add(42);

  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());
  extension->initializeInModuleExtension();

  // Test getting a counter value.
  uint64_t value = 0;
  envoy_dynamic_module_type_module_buffer name{"test.counter", 12};
  bool found = envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
      static_cast<void*>(extension.get()), name, &value);

  EXPECT_TRUE(found);
  EXPECT_EQ(value, 42u);
}

// Test get_counter_value callback with a non-existent counter.
TEST_F(BootstrapAbiImplTest, GetCounterValueNonExistent) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());
  extension->initializeInModuleExtension();

  // Test getting a non-existent counter.
  uint64_t value = 0;
  envoy_dynamic_module_type_module_buffer name{"non.existent", 12};
  bool found = envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
      static_cast<void*>(extension.get()), name, &value);

  EXPECT_FALSE(found);
}

// Test get_gauge_value callback with an existing gauge.
TEST_F(BootstrapAbiImplTest, GetGaugeValueExisting) {
  // Create a gauge in the stats store.
  context_.store_.gaugeFromString("test.gauge", Stats::Gauge::ImportMode::Accumulate).set(123);

  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());
  extension->initializeInModuleExtension();

  // Test getting a gauge value.
  uint64_t value = 0;
  envoy_dynamic_module_type_module_buffer name{"test.gauge", 10};
  bool found = envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
      static_cast<void*>(extension.get()), name, &value);

  EXPECT_TRUE(found);
  EXPECT_EQ(value, 123u);
}

// Test get_gauge_value callback with a non-existent gauge.
TEST_F(BootstrapAbiImplTest, GetGaugeValueNonExistent) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());
  extension->initializeInModuleExtension();

  // Test getting a non-existent gauge.
  uint64_t value = 0;
  envoy_dynamic_module_type_module_buffer name{"non.existent", 12};
  bool found = envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
      static_cast<void*>(extension.get()), name, &value);

  EXPECT_FALSE(found);
}

// Test iterate_counters callback.
TEST_F(BootstrapAbiImplTest, IterateCounters) {
  // Create some counters in the stats store.
  context_.store_.counterFromString("counter.one").add(1);
  context_.store_.counterFromString("counter.two").add(2);
  context_.store_.counterFromString("counter.three").add(3);

  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());
  extension->initializeInModuleExtension();

  // Track visited counters.
  struct VisitorData {
    int count;
  };
  VisitorData data{0};

  auto iterator = [](envoy_dynamic_module_type_envoy_buffer, uint64_t,
                     void* user_data) -> envoy_dynamic_module_type_stats_iteration_action {
    auto* d = static_cast<VisitorData*>(user_data);
    d->count++;
    return envoy_dynamic_module_type_stats_iteration_action_Continue;
  };

  envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(
      static_cast<void*>(extension.get()), iterator, &data);

  EXPECT_EQ(data.count, 3);
}

// Test iterate_gauges callback.
TEST_F(BootstrapAbiImplTest, IterateGauges) {
  // Create some gauges in the stats store.
  context_.store_.gaugeFromString("gauge.one", Stats::Gauge::ImportMode::Accumulate).set(1);
  context_.store_.gaugeFromString("gauge.two", Stats::Gauge::ImportMode::Accumulate).set(2);

  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());
  extension->initializeInModuleExtension();

  // Track visited gauges.
  struct VisitorData {
    int count;
  };
  VisitorData data{0};

  auto iterator = [](envoy_dynamic_module_type_envoy_buffer, uint64_t,
                     void* user_data) -> envoy_dynamic_module_type_stats_iteration_action {
    auto* d = static_cast<VisitorData*>(user_data);
    d->count++;
    return envoy_dynamic_module_type_stats_iteration_action_Continue;
  };

  envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(
      static_cast<void*>(extension.get()), iterator, &data);

  EXPECT_EQ(data.count, 2);
}

// -----------------------------------------------------------------------------
// Stats Definition and Update Tests
// -----------------------------------------------------------------------------

// Test defining and incrementing a counter without labels.
TEST_F(BootstrapAbiImplTest, DefineAndIncrementCounter) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a counter without labels.
  size_t counter_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_counter", 10};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
      config.value()->thisAsVoidPtr(), name, nullptr, 0, &counter_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_id, 0u);

  // Increment the counter.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
      config.value()->thisAsVoidPtr(), counter_id, nullptr, 0, 5);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Increment again.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
      config.value()->thisAsVoidPtr(), counter_id, nullptr, 0, 3);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify the counter was defined and is accessible.
  EXPECT_TRUE(config.value()->getCounterById(counter_id).has_value());
  EXPECT_FALSE(config.value()->getCounterById(counter_id + 1).has_value());
}

// Test incrementing a counter with an invalid ID.
TEST_F(BootstrapAbiImplTest, IncrementCounterInvalidId) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Try to increment a counter with an invalid ID.
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
      config.value()->thisAsVoidPtr(), 999, nullptr, 0, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// Test defining and manipulating a gauge without labels.
TEST_F(BootstrapAbiImplTest, DefineAndManipulateGauge) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a gauge without labels.
  size_t gauge_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_gauge", 8};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
      config.value()->thisAsVoidPtr(), name, nullptr, 0, &gauge_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_id, 0u);

  // Set the gauge.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
      config.value()->thisAsVoidPtr(), gauge_id, nullptr, 0, 100);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Increment the gauge.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
      config.value()->thisAsVoidPtr(), gauge_id, nullptr, 0, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Decrement the gauge.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
      config.value()->thisAsVoidPtr(), gauge_id, nullptr, 0, 30);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify the gauge was defined and is accessible.
  EXPECT_TRUE(config.value()->getGaugeById(gauge_id).has_value());
  EXPECT_FALSE(config.value()->getGaugeById(gauge_id + 1).has_value());
}

// Test gauge operations with an invalid ID.
TEST_F(BootstrapAbiImplTest, GaugeOperationsInvalidId) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Try to set, increment, and decrement a gauge with an invalid ID.
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
                config.value()->thisAsVoidPtr(), 999, nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
                config.value()->thisAsVoidPtr(), 999, nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
                config.value()->thisAsVoidPtr(), 999, nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// Test defining and recording a histogram value without labels.
TEST_F(BootstrapAbiImplTest, DefineAndRecordHistogram) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a histogram without labels.
  size_t histogram_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_histogram", 12};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
      config.value()->thisAsVoidPtr(), name, nullptr, 0, &histogram_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(histogram_id, 0u);

  // Record a value.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
      config.value()->thisAsVoidPtr(), histogram_id, nullptr, 0, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Record another value.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
      config.value()->thisAsVoidPtr(), histogram_id, nullptr, 0, 100);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
}

// Test recording a histogram value with an invalid ID.
TEST_F(BootstrapAbiImplTest, RecordHistogramInvalidId) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Try to record a histogram value with an invalid ID.
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
      config.value()->thisAsVoidPtr(), 999, nullptr, 0, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// Test defining multiple metrics with sequential IDs.
TEST_F(BootstrapAbiImplTest, DefineMultipleMetrics) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define multiple counters.
  size_t counter_id_0 = 0;
  size_t counter_id_1 = 0;
  envoy_dynamic_module_type_module_buffer name_0 = {"counter_a", 9};
  envoy_dynamic_module_type_module_buffer name_1 = {"counter_b", 9};
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
                config.value()->thisAsVoidPtr(), name_0, nullptr, 0, &counter_id_0),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
                config.value()->thisAsVoidPtr(), name_1, nullptr, 0, &counter_id_1),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_id_0, 0u);
  EXPECT_EQ(counter_id_1, 1u);

  // Define multiple gauges.
  size_t gauge_id_0 = 0;
  size_t gauge_id_1 = 0;
  envoy_dynamic_module_type_module_buffer gauge_name_0 = {"gauge_a", 7};
  envoy_dynamic_module_type_module_buffer gauge_name_1 = {"gauge_b", 7};
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
                config.value()->thisAsVoidPtr(), gauge_name_0, nullptr, 0, &gauge_id_0),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
                config.value()->thisAsVoidPtr(), gauge_name_1, nullptr, 0, &gauge_id_1),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_id_0, 0u);
  EXPECT_EQ(gauge_id_1, 1u);

  // Increment each counter independently.
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
                config.value()->thisAsVoidPtr(), counter_id_0, nullptr, 0, 10),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
                config.value()->thisAsVoidPtr(), counter_id_1, nullptr, 0, 20),
            envoy_dynamic_module_type_metrics_result_Success);

  // Verify all counters and gauges are accessible by their IDs.
  EXPECT_TRUE(config.value()->getCounterById(counter_id_0).has_value());
  EXPECT_TRUE(config.value()->getCounterById(counter_id_1).has_value());
  EXPECT_TRUE(config.value()->getGaugeById(gauge_id_0).has_value());
  EXPECT_TRUE(config.value()->getGaugeById(gauge_id_1).has_value());
}

// -----------------------------------------------------------------------------
// Stats Definition and Update Tests (with labels / vec variants)
// -----------------------------------------------------------------------------

// Test defining and incrementing a counter vec with labels.
TEST_F(BootstrapAbiImplTest, DefineAndIncrementCounterVec) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a counter vec with labels.
  size_t counter_vec_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_counter_vec", 14};
  envoy_dynamic_module_type_module_buffer label_names[] = {{"method", 6}, {"status", 6}};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
      config.value()->thisAsVoidPtr(), name, label_names, 2, &counter_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_vec_id, 0u);

  // Increment the counter vec with matching labels.
  envoy_dynamic_module_type_module_buffer label_values[] = {{"GET", 3}, {"200", 3}};
  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
      config.value()->thisAsVoidPtr(), counter_vec_id, label_values, 2, 5);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify the counter vec was defined and is accessible.
  EXPECT_TRUE(config.value()->getCounterVecById(counter_vec_id).has_value());
  EXPECT_FALSE(config.value()->getCounterVecById(counter_vec_id + 1).has_value());
}

// Test incrementing a counter vec with mismatched label count.
TEST_F(BootstrapAbiImplTest, IncrementCounterVecInvalidLabels) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a counter vec with 2 labels.
  size_t counter_vec_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_counter_vec", 14};
  envoy_dynamic_module_type_module_buffer label_names[] = {{"method", 6}, {"status", 6}};
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
                config.value()->thisAsVoidPtr(), name, label_names, 2, &counter_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);

  // Try to increment with only 1 label value (mismatched).
  envoy_dynamic_module_type_module_buffer label_values[] = {{"GET", 3}};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
      config.value()->thisAsVoidPtr(), counter_vec_id, label_values, 1, 5);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

// Test defining and manipulating a gauge vec with labels.
TEST_F(BootstrapAbiImplTest, DefineAndManipulateGaugeVec) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a gauge vec with labels.
  size_t gauge_vec_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_gauge_vec", 12};
  envoy_dynamic_module_type_module_buffer label_names[] = {{"host", 4}};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
      config.value()->thisAsVoidPtr(), name, label_names, 1, &gauge_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_vec_id, 0u);

  // Set, increment, and decrement the gauge vec with matching labels.
  envoy_dynamic_module_type_module_buffer label_values[] = {{"upstream_a", 10}};
  result = envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
      config.value()->thisAsVoidPtr(), gauge_vec_id, label_values, 1, 100);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
      config.value()->thisAsVoidPtr(), gauge_vec_id, label_values, 1, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  result = envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
      config.value()->thisAsVoidPtr(), gauge_vec_id, label_values, 1, 5);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify the gauge vec was defined and is accessible.
  EXPECT_TRUE(config.value()->getGaugeVecById(gauge_vec_id).has_value());
}

// Test defining and recording a histogram vec with labels.
TEST_F(BootstrapAbiImplTest, DefineAndRecordHistogramVec) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a histogram vec with labels.
  size_t histogram_vec_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_histogram_vec", 16};
  envoy_dynamic_module_type_module_buffer label_names[] = {{"endpoint", 8}};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
      config.value()->thisAsVoidPtr(), name, label_names, 1, &histogram_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(histogram_vec_id, 0u);

  // Record values with matching labels.
  envoy_dynamic_module_type_module_buffer label_values[] = {{"svc_a", 5}};
  result = envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
      config.value()->thisAsVoidPtr(), histogram_vec_id, label_values, 1, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify the histogram vec was defined and is accessible.
  EXPECT_TRUE(config.value()->getHistogramVecById(histogram_vec_id).has_value());
}

// Test vec metric operations with an invalid vec ID and mismatched label count.
// This covers the vec code paths for all metric types.
TEST_F(BootstrapAbiImplTest, VecMetricsInvalidIdAndLabels) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define vec metrics with a single label each.
  size_t counter_vec_id = 0;
  size_t gauge_vec_id = 0;
  size_t histogram_vec_id = 0;
  envoy_dynamic_module_type_module_buffer counter_name = {"cv", 2};
  envoy_dynamic_module_type_module_buffer gauge_name = {"gv", 2};
  envoy_dynamic_module_type_module_buffer histogram_name = {"hv", 2};
  envoy_dynamic_module_type_module_buffer label_names[] = {{"lbl", 3}};
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
                config.value()->thisAsVoidPtr(), counter_name, label_names, 1, &counter_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
                config.value()->thisAsVoidPtr(), gauge_name, label_names, 1, &gauge_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
                config.value()->thisAsVoidPtr(), histogram_name, label_names, 1, &histogram_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);

  // Use a valid label value for MetricNotFound tests.
  envoy_dynamic_module_type_module_buffer one_label[] = {{"val", 3}};
  size_t invalid_id = 999;

  // Test MetricNotFound for all vec update operations with an invalid vec ID.
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
                config.value()->thisAsVoidPtr(), invalid_id, one_label, 1, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
                config.value()->thisAsVoidPtr(), invalid_id, one_label, 1, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
                config.value()->thisAsVoidPtr(), invalid_id, one_label, 1, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
                config.value()->thisAsVoidPtr(), invalid_id, one_label, 1, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
                config.value()->thisAsVoidPtr(), invalid_id, one_label, 1, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);

  // Use two label values to trigger InvalidLabels (defined with 1 label, passing 2).
  envoy_dynamic_module_type_module_buffer two_labels[] = {{"a", 1}, {"b", 1}};

  // Test InvalidLabels for all vec update operations with mismatched label count.
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
                config.value()->thisAsVoidPtr(), counter_vec_id, two_labels, 2, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
                config.value()->thisAsVoidPtr(), gauge_vec_id, two_labels, 2, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
                config.value()->thisAsVoidPtr(), gauge_vec_id, two_labels, 2, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
                config.value()->thisAsVoidPtr(), gauge_vec_id, two_labels, 2, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
                config.value()->thisAsVoidPtr(), histogram_vec_id, two_labels, 2, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

// -----------------------------------------------------------------------------
// Timer Tests
// -----------------------------------------------------------------------------

// Test that a timer can be created, enabled, checked, disabled, and deleted.
TEST_F(BootstrapAbiImplTest, TimerLifecycle) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  // The MockDispatcher's createTimer_ returns a NiceMock<MockTimer> by default.
  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a timer via the ABI callback.
  auto* timer_ptr =
      envoy_dynamic_module_callback_bootstrap_extension_timer_new(config.value()->thisAsVoidPtr());
  EXPECT_NE(timer_ptr, nullptr);

  // The timer should not be enabled initially.
  EXPECT_FALSE(envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(timer_ptr));

  // Enable the timer with a 100ms delay.
  envoy_dynamic_module_callback_bootstrap_extension_timer_enable(timer_ptr, 100);
  EXPECT_TRUE(envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(timer_ptr));

  // Disable the timer.
  envoy_dynamic_module_callback_bootstrap_extension_timer_disable(timer_ptr);
  EXPECT_FALSE(envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(timer_ptr));

  // Delete the timer via the ABI callback.
  envoy_dynamic_module_callback_bootstrap_extension_timer_delete(timer_ptr);
}

// Test that the timer fires and invokes the on_timer_fired event hook.
TEST_F(BootstrapAbiImplTest, TimerFired) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  // Use MockTimer to capture the timer callback.
  Event::MockTimer* mock_timer = new Event::MockTimer(&dispatcher_);

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a timer via the ABI callback. This will use the MockTimer we set up.
  auto* timer_ptr =
      envoy_dynamic_module_callback_bootstrap_extension_timer_new(config.value()->thisAsVoidPtr());
  EXPECT_NE(timer_ptr, nullptr);

  // Enable the timer.
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(50), _));
  envoy_dynamic_module_callback_bootstrap_extension_timer_enable(timer_ptr, 50);

  // Invoke the timer callback (simulating timer firing).
  mock_timer->invokeCallback();

  // Clean up.
  envoy_dynamic_module_callback_bootstrap_extension_timer_delete(timer_ptr);
}

// Test that the timer callback safely handles a destroyed config via weak_ptr.
TEST_F(BootstrapAbiImplTest, TimerFiredAfterConfigDestroyed) {
  Event::TimerCb captured_timer_cb;

  // Use a raw pointer so we can control when the config is destroyed.
  void* timer_ptr = nullptr;

  {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        testDataDir() + "/libbootstrap_no_op.so", false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

    // Capture the timer callback from createTimer.
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(testing::Invoke([&](Event::TimerCb cb) {
      captured_timer_cb = std::move(cb);
      return new testing::NiceMock<Event::MockTimer>();
    }));

    auto config = newDynamicModuleBootstrapExtensionConfig("test", "config",
                                                           std::move(dynamic_module.value()),
                                                           dispatcher_, context_, context_.store_);
    ASSERT_TRUE(config.ok()) << config.status();

    // Create a timer via the ABI callback.
    timer_ptr = envoy_dynamic_module_callback_bootstrap_extension_timer_new(
        config.value()->thisAsVoidPtr());
    EXPECT_NE(timer_ptr, nullptr);

    // Config goes out of scope here and is destroyed.
  }

  // Execute the captured timer callback after config is destroyed.
  // This should not crash - the weak_ptr should be expired.
  ASSERT_NE(captured_timer_cb, nullptr);
  captured_timer_cb();

  // Clean up the timer.
  envoy_dynamic_module_callback_bootstrap_extension_timer_delete(timer_ptr);
}

// -----------------------------------------------------------------------------
// Admin Handler Tests
// -----------------------------------------------------------------------------

// Test that registering an admin handler succeeds when admin is available.
TEST_F(BootstrapAbiImplTest, RegisterAdminHandler) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Expect the admin handler to be registered.
  EXPECT_CALL(context_.admin_, addHandler("/test_prefix", "Test help text", _, true, false, _))
      .WillOnce(testing::Return(true));

  envoy_dynamic_module_type_module_buffer path_prefix = {"/test_prefix", 12};
  envoy_dynamic_module_type_module_buffer help_text = {"Test help text", 14};
  bool result = envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
      config.value()->thisAsVoidPtr(), path_prefix, help_text, true, false);
  EXPECT_TRUE(result);
}

// Test that registering an admin handler fails when addHandler returns false.
TEST_F(BootstrapAbiImplTest, RegisterAdminHandlerFails) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Expect the admin handler registration to fail.
  EXPECT_CALL(context_.admin_, addHandler("/duplicate", "Duplicate handler", _, false, true, _))
      .WillOnce(testing::Return(false));

  envoy_dynamic_module_type_module_buffer path_prefix = {"/duplicate", 10};
  envoy_dynamic_module_type_module_buffer help_text = {"Duplicate handler", 17};
  bool result = envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
      config.value()->thisAsVoidPtr(), path_prefix, help_text, false, true);
  EXPECT_FALSE(result);
}

// Test that registering an admin handler fails when admin is not available.
TEST_F(BootstrapAbiImplTest, RegisterAdminHandlerNoAdmin) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  // Override admin() to return nullopt.
  EXPECT_CALL(context_, admin()).WillRepeatedly(testing::Return(OptRef<Server::Admin>{}));

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  envoy_dynamic_module_type_module_buffer path_prefix = {"/no_admin", 9};
  envoy_dynamic_module_type_module_buffer help_text = {"No admin", 8};
  bool result = envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
      config.value()->thisAsVoidPtr(), path_prefix, help_text, true, false);
  EXPECT_FALSE(result);
}

// Test that removing an admin handler succeeds.
TEST_F(BootstrapAbiImplTest, RemoveAdminHandler) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  EXPECT_CALL(context_.admin_, removeHandler("/test_prefix")).WillOnce(testing::Return(true));

  envoy_dynamic_module_type_module_buffer path_prefix = {"/test_prefix", 12};
  bool result = envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
      config.value()->thisAsVoidPtr(), path_prefix);
  EXPECT_TRUE(result);
}

// Test that removing an admin handler fails when handler is not found.
TEST_F(BootstrapAbiImplTest, RemoveAdminHandlerNotFound) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  EXPECT_CALL(context_.admin_, removeHandler("/nonexistent")).WillOnce(testing::Return(false));

  envoy_dynamic_module_type_module_buffer path_prefix = {"/nonexistent", 12};
  bool result = envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
      config.value()->thisAsVoidPtr(), path_prefix);
  EXPECT_FALSE(result);
}

// Test that removing an admin handler fails when admin is not available.
TEST_F(BootstrapAbiImplTest, RemoveAdminHandlerNoAdmin) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  // Override admin() to return nullopt.
  EXPECT_CALL(context_, admin()).WillRepeatedly(testing::Return(OptRef<Server::Admin>{}));

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  envoy_dynamic_module_type_module_buffer path_prefix = {"/no_admin", 9};
  bool result = envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
      config.value()->thisAsVoidPtr(), path_prefix);
  EXPECT_FALSE(result);
}

// Test that the admin handler callback invokes the module's on_admin_request event hook.
TEST_F(BootstrapAbiImplTest, AdminHandlerCallbackInvokesEventHook) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Capture the handler callback when addHandler is called.
  Server::Admin::HandlerCb captured_handler;
  EXPECT_CALL(context_.admin_, addHandler("/test_admin", "Test admin handler", _, true, false, _))
      .WillOnce(
          testing::Invoke([&](const std::string&, const std::string&, Server::Admin::HandlerCb cb,
                              bool, bool, const Server::Admin::ParamDescriptorVec&) -> bool {
            captured_handler = std::move(cb);
            return true;
          }));

  envoy_dynamic_module_type_module_buffer path_prefix = {"/test_admin", 11};
  envoy_dynamic_module_type_module_buffer help_text = {"Test admin handler", 18};
  bool result = envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
      config.value()->thisAsVoidPtr(), path_prefix, help_text, true, false);
  EXPECT_TRUE(result);
  EXPECT_NE(captured_handler, nullptr);

  // Invoke the captured handler to simulate an admin request.
  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response_body;
  testing::NiceMock<Server::MockAdminStream> admin_stream;

  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.setMethod("GET");
  request_headers.setPath("/test_admin?param=value");
  EXPECT_CALL(admin_stream, getRequestHeaders())
      .WillRepeatedly(testing::ReturnRef(request_headers));
  EXPECT_CALL(admin_stream, getRequestBody()).WillRepeatedly(testing::Return(nullptr));

  auto code = captured_handler(response_headers, response_body, admin_stream);

  // The no_op module's admin_request hook returns 200.
  EXPECT_EQ(code, Http::Code::OK);
}

// Test that re-enabling the timer resets it.
TEST_F(BootstrapAbiImplTest, TimerReEnable) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  Event::MockTimer* mock_timer = new Event::MockTimer(&dispatcher_);

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a timer via the ABI callback.
  auto* timer_ptr =
      envoy_dynamic_module_callback_bootstrap_extension_timer_new(config.value()->thisAsVoidPtr());
  EXPECT_NE(timer_ptr, nullptr);

  // Enable with 100ms.
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(100), _));
  envoy_dynamic_module_callback_bootstrap_extension_timer_enable(timer_ptr, 100);

  // Re-enable with 200ms - this should reset the timer.
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(200), _));
  envoy_dynamic_module_callback_bootstrap_extension_timer_enable(timer_ptr, 200);

  // Clean up.
  envoy_dynamic_module_callback_bootstrap_extension_timer_delete(timer_ptr);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
