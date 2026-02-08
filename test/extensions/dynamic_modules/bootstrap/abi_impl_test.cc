#include "source/extensions/bootstrap/dynamic_modules/extension.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
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

// Test defining and incrementing a counter.
TEST_F(BootstrapAbiImplTest, DefineAndIncrementCounter) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a counter.
  size_t counter_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_counter", 10};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
      config.value()->thisAsVoidPtr(), name, &counter_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_id, 0u);

  // Increment the counter.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
      config.value()->thisAsVoidPtr(), counter_id, 5);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Increment again.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
      config.value()->thisAsVoidPtr(), counter_id, 3);
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
      config.value()->thisAsVoidPtr(), 999, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// Test defining and manipulating a gauge.
TEST_F(BootstrapAbiImplTest, DefineAndManipulateGauge) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a gauge.
  size_t gauge_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_gauge", 8};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
      config.value()->thisAsVoidPtr(), name, &gauge_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_id, 0u);

  // Set the gauge.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
      config.value()->thisAsVoidPtr(), gauge_id, 100);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Increment the gauge.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
      config.value()->thisAsVoidPtr(), gauge_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Decrement the gauge.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
      config.value()->thisAsVoidPtr(), gauge_id, 30);
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
                config.value()->thisAsVoidPtr(), 999, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
                config.value()->thisAsVoidPtr(), 999, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
                config.value()->thisAsVoidPtr(), 999, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// Test defining and recording a histogram value.
TEST_F(BootstrapAbiImplTest, DefineAndRecordHistogram) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Define a histogram.
  size_t histogram_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"my_histogram", 12};
  auto result = envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
      config.value()->thisAsVoidPtr(), name, &histogram_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(histogram_id, 0u);

  // Record a value.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
      config.value()->thisAsVoidPtr(), histogram_id, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Record another value.
  result = envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
      config.value()->thisAsVoidPtr(), histogram_id, 100);
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
      config.value()->thisAsVoidPtr(), 999, 42);
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
                config.value()->thisAsVoidPtr(), name_0, &counter_id_0),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
                config.value()->thisAsVoidPtr(), name_1, &counter_id_1),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_id_0, 0u);
  EXPECT_EQ(counter_id_1, 1u);

  // Define multiple gauges.
  size_t gauge_id_0 = 0;
  size_t gauge_id_1 = 0;
  envoy_dynamic_module_type_module_buffer gauge_name_0 = {"gauge_a", 7};
  envoy_dynamic_module_type_module_buffer gauge_name_1 = {"gauge_b", 7};
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
                config.value()->thisAsVoidPtr(), gauge_name_0, &gauge_id_0),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
                config.value()->thisAsVoidPtr(), gauge_name_1, &gauge_id_1),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_id_0, 0u);
  EXPECT_EQ(gauge_id_1, 1u);

  // Increment each counter independently.
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
                config.value()->thisAsVoidPtr(), counter_id_0, 10),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
                config.value()->thisAsVoidPtr(), counter_id_1, 20),
            envoy_dynamic_module_type_metrics_result_Success);

  // Verify all counters and gauges are accessible by their IDs.
  EXPECT_TRUE(config.value()->getCounterById(counter_id_0).has_value());
  EXPECT_TRUE(config.value()->getCounterById(counter_id_1).has_value());
  EXPECT_TRUE(config.value()->getGaugeById(gauge_id_0).has_value());
  EXPECT_TRUE(config.value()->getGaugeById(gauge_id_1).has_value());
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
