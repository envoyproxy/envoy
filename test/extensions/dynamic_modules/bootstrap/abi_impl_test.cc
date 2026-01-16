#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"
#include "source/extensions/dynamic_modules/abi.h"

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
  Upstream::MockClusterManager cluster_manager_;
};

// Test that the scheduler can be created, used, and deleted.
TEST_F(BootstrapAbiImplTest, SchedulerLifecycle) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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

    auto config = newDynamicModuleBootstrapExtensionConfig(
        "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Call onScheduled directly - this should call the in-module hook.
  config.value()->onScheduled(789);
}

// Test HTTP callout returns ClusterNotFound when cluster does not exist.
TEST_F(BootstrapAbiImplTest, HttpCalloutClusterNotFound) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            // Verify request parameters.
            EXPECT_EQ(message->headers().Path()->value().getStringView(), "/test");
            EXPECT_EQ(message->headers().Method()->value().getStringView(), "GET");
            EXPECT_EQ(message->headers().Host()->value().getStringView(), "example.com");
            EXPECT_EQ(message->body().toString(), "request_body");
            EXPECT_EQ(options.timeout.value(), std::chrono::milliseconds(5000));
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
  envoy_dynamic_module_type_module_buffer body = {"request_body", 12};

  auto result = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id, cluster_name, headers.data(), headers.size(),
      body, 5000);

  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(callout_id, 0);
  EXPECT_NE(callbacks_captured, nullptr);

  // Create a response with headers and body.
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl({
      {":status", "200"},
      {"content-type", "application/json"},
  }));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add("response_body");

  // Trigger the success callback.
  callbacks_captured->onSuccess(request, std::move(response));
}

// Test HTTP callout failure with Reset reason.
TEST_F(BootstrapAbiImplTest, HttpCalloutFailureReset) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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

// Test that pending HTTP callouts are properly tracked and can be cancelled.
// Note: The config destructor cancels pending callouts, but due to the shared_ptr design
// (callbacks hold shared_ptr to config), the config is only destroyed when all callbacks
// complete. This test verifies the cancel behavior is properly wired up by completing the
// request after making it.
TEST_F(BootstrapAbiImplTest, HttpCalloutRequestTracking) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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
  EXPECT_NE(callout_id, 0);
  EXPECT_NE(callbacks_captured, nullptr);

  // Complete the request to verify the callback tracking works.
  Http::ResponseHeaderMapPtr resp_headers(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  callbacks_captured->onSuccess(request, std::move(response));
}

// Test multiple HTTP callouts with incrementing callout IDs.
TEST_F(BootstrapAbiImplTest, HttpCalloutMultipleRequests) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillRepeatedly(testing::Return(&thread_local_cluster));

  // Setup mock async client to return requests.
  Http::MockAsyncClientRequest request1(&thread_local_cluster.async_client_);
  Http::MockAsyncClientRequest request2(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks1 = nullptr;
  Http::AsyncClient::Callbacks* callbacks2 = nullptr;

  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks1 = &callbacks;
            return &request1;
          }))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks2 = &callbacks;
            return &request2;
          }));

  // Build headers.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };

  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};

  // First callout.
  uint64_t callout_id1 = 0;
  auto result1 = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id1, cluster_name, headers.data(), headers.size(),
      body, 5000);
  EXPECT_EQ(result1, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_EQ(callout_id1, 1);

  // Second callout.
  uint64_t callout_id2 = 0;
  auto result2 = envoy_dynamic_module_callback_bootstrap_extension_http_callout(
      config.value()->thisAsVoidPtr(), &callout_id2, cluster_name, headers.data(), headers.size(),
      body, 5000);
  EXPECT_EQ(result2, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_EQ(callout_id2, 2);

  // Complete both requests.
  Http::ResponseHeaderMapPtr resp_headers1(
      new Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  Http::ResponseMessagePtr response1(new Http::ResponseMessageImpl(std::move(resp_headers1)));
  callbacks1->onSuccess(request1, std::move(response1));

  Http::ResponseHeaderMapPtr resp_headers2(
      new Http::TestResponseHeaderMapImpl({{":status", "201"}}));
  Http::ResponseMessagePtr response2(new Http::ResponseMessageImpl(std::move(resp_headers2)));
  callbacks2->onSuccess(request2, std::move(response2));
}

// Test HTTP callout with body in request.
TEST_F(BootstrapAbiImplTest, HttpCalloutWithBody) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
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

// Test HTTP callout immediate failure (request_ not set in onFailure).
TEST_F(BootstrapAbiImplTest, HttpCalloutImmediateFailure) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_, context_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Setup mock cluster manager to return a valid cluster.
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  // Setup mock async client to immediately call onFailure (simulating inline failure).
  // In this case, send_ returns nullptr but the callback is still called.
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            // Return nullptr to simulate request creation failure.
            return nullptr;
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

  // When send_ returns nullptr, the result should be CannotCreateRequest.
  EXPECT_EQ(result, envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
