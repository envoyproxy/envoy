#include "envoy/config/core/v3/config_source.pb.h"

#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

using ::Envoy::StatusHelpers::HasStatusMessage;

class ExtensionConfigTest : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(ExtensionConfigTest, LoadOK) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  ASSERT_OK(config);
  EXPECT_NE(config.value()->in_module_config_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_config_destroy_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_new_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_server_initialized_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_worker_thread_initialized_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_destroy_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_drain_started_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_shutdown_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_config_scheduled_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_http_callout_done_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_timer_fired_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_admin_request_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_secret_add_or_update_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_secret_removal_, nullptr);
}

TEST_F(ExtensionConfigTest, ConfigNewFail) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_config_new.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage("Failed to initialize dynamic module"));
}

TEST_F(ExtensionConfigTest, MissingConfigDestroy) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_config_destroy.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_config_destroy")));
}

TEST_F(ExtensionConfigTest, MissingExtensionNew) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_extension_new.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(
                          testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_new")));
}

TEST_F(ExtensionConfigTest, MissingServerInitialized) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_server_initialized.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_server_initialized")));
}

TEST_F(ExtensionConfigTest, MissingWorkerThreadInitialized) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_worker_initialized.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config,
              HasStatusMessage(testing::HasSubstr(
                  "envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized")));
}

TEST_F(ExtensionConfigTest, MissingExtensionDestroy) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_extension_destroy.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_destroy")));
}

TEST_F(ExtensionConfigTest, MissingConstructor) {
  // Test that config creation fails when envoy_dynamic_module_on_bootstrap_extension_config_new
  // symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_constructor.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_config_new")));
}

TEST_F(ExtensionConfigTest, MissingDrainStarted) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_drain_started.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_drain_started")));
}

TEST_F(ExtensionConfigTest, MissingShutdown) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_shutdown.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_shutdown")));
}

TEST_F(ExtensionConfigTest, MissingConfigScheduled) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_config_scheduled symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_config_scheduled.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_config_scheduled")));
}

TEST_F(ExtensionConfigTest, MissingHttpCalloutDone) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_http_callout_done symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_http_callout_done.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_http_callout_done")));
}

TEST_F(ExtensionConfigTest, MissingTimerFired) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_timer_fired symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_timer_fired.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_timer_fired")));
}

TEST_F(ExtensionConfigTest, MissingFileChanged) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_file_changed symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_file_changed.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_file_changed")));
}

TEST_F(ExtensionConfigTest, MissingAdminRequest) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_admin_request symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_admin_request.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_admin_request")));
}

TEST_F(ExtensionConfigTest, MissingClusterAddOrUpdate) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_cluster_add_or_update.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update")));
}

TEST_F(ExtensionConfigTest, MissingClusterRemoval) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_cluster_removal symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_cluster_removal.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_cluster_removal")));
}

TEST_F(ExtensionConfigTest, MissingListenerAddOrUpdate) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_listener_add_or_update.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update")));
}

TEST_F(ExtensionConfigTest, MissingListenerRemoval) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_listener_removal symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_listener_removal.so", false);
  ASSERT_OK(dynamic_module);

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  EXPECT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "envoy_dynamic_module_on_bootstrap_extension_listener_removal")));
}

TEST_F(ExtensionConfigTest, ClusterAccessRequiresServerInitialized) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_OK(dynamic_module);
  auto config_or = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", DefaultMetricsNamespace, std::move(dynamic_module.value()), dispatcher_,
      context_, context_.store_);
  ASSERT_OK(config_or);
  auto config = config_or.value();

  // Before the server is initialized the cluster manager is unavailable, so cluster access is
  // refused rather than dereferencing a null cluster manager.
  EXPECT_FALSE(config->enableClusterLifecycle());
  uint64_t callout_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound,
            config->sendHttpCallout(&callout_id, "some_cluster",
                                    std::make_unique<Http::RequestMessageImpl>(), 1000));

  // After the server is initialized cluster lifecycle can be enabled.
  testing::NiceMock<Server::MockListenerManager> listener_manager;
  config->setListenerManager(listener_manager);
  EXPECT_TRUE(config->enableClusterLifecycle());
}

TEST_F(ExtensionConfigTest, MissingSecretAddOrUpdate) {
  // The secret lifecycle hooks are optional: a module that omits
  // envoy_dynamic_module_on_bootstrap_extension_secret_add_or_update still loads, with that hook
  // left null while the sibling hook it does export resolves.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_secret_add_or_update.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config.value()->on_bootstrap_extension_secret_add_or_update_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_secret_removal_, nullptr);
}

TEST_F(ExtensionConfigTest, MissingSecretRemoval) {
  // The secret lifecycle hooks are optional: a module that omits
  // envoy_dynamic_module_on_bootstrap_extension_secret_removal still loads, with that hook left
  // null while the sibling hook it does export resolves.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_secret_removal.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig("test", "config", DefaultMetricsNamespace,
                                                         std::move(dynamic_module.value()),
                                                         dispatcher_, context_, context_.store_);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config.value()->on_bootstrap_extension_secret_removal_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_secret_add_or_update_, nullptr);
}

TEST_F(ExtensionConfigTest, ClusterCallbacksMarshaledToMainThread) {
  // Cluster lifecycle callbacks are delivered by the ClusterManager on every worker thread
  // (runOnAllThreads), but the module hooks may only run on the main thread. Both callbacks must
  // therefore reach the module via the main-thread dispatcher's post(), not inline. This guards the
  // historical worker-thread crash.
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();
  auto config_or = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", DefaultMetricsNamespace, std::move(dynamic_module.value()), dispatcher_,
      context_, context_.store_);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  auto config = config_or.value();

  testing::NiceMock<Server::MockListenerManager> listener_manager;
  config->setListenerManager(listener_manager);
  ASSERT_TRUE(config->enableClusterLifecycle());

  int posts = 0;
  EXPECT_CALL(dispatcher_, post(testing::_)).Times(2).WillRepeatedly([&posts](Event::PostCb cb) {
    ++posts;
    // Simulate the main-thread turn running the marshaled work.
    cb();
  });

  testing::NiceMock<Upstream::MockThreadLocalCluster> cluster;
  Upstream::ThreadLocalClusterCommand command = [&cluster]() -> Upstream::ThreadLocalCluster& {
    return cluster;
  };
  config->onClusterAddOrUpdate("some_cluster", command);
  config->onClusterRemoval("some_cluster");
  EXPECT_EQ(posts, 2);
}

TEST_F(ExtensionConfigTest, EnableSecretLifecycleRequiresServerInitialized) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();
  auto config_or = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", DefaultMetricsNamespace, std::move(dynamic_module.value()), dispatcher_,
      context_, context_.store_);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  auto config = config_or.value();

  // Before the server is initialized the SecretManager is unavailable, so enabling is refused.
  EXPECT_FALSE(config->enableSecretLifecycle());

  testing::NiceMock<Server::MockListenerManager> listener_manager;
  config->setListenerManager(listener_manager);
  // After initialization it can be enabled once; a second call is a no-op.
  EXPECT_TRUE(config->enableSecretLifecycle());
  EXPECT_FALSE(config->enableSecretLifecycle());
}

// A dynamic secret removal reaches the module's on_secret_removal hook. The mock context is backed
// by a real SecretManager: a TLS certificate provider is created, secret lifecycle is enabled so
// the config hooks the provider's remove callback, and a delta SDS removal then fires it.
TEST_F(ExtensionConfigTest, SecretRemovalDeliveredToModule) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr provider_dispatcher = api->allocateDispatcher("test_thread");
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;
  envoy::config::core::v3::ConfigSource config_source;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info;
  testing::NiceMock<Init::MockManager> init_manager;
  testing::NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(testing::_))
      .WillOnce(testing::Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context.server_context_, mainThreadDispatcher())
      .WillRepeatedly(testing::ReturnRef(*provider_dispatcher));
  EXPECT_CALL(secret_context.server_context_, localInfo())
      .WillRepeatedly(testing::ReturnRef(local_info));
  EXPECT_CALL(secret_context.server_context_, api()).WillRepeatedly(testing::ReturnRef(*api));

  auto provider = context_.secretManager().findOrCreateTlsCertificateProvider(
      config_source, "secret_0", secret_context.server_context_, init_manager, true);
  ASSERT_NE(provider, nullptr);

  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();
  auto config_or = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", DefaultMetricsNamespace, std::move(dynamic_module.value()), dispatcher_,
      context_, context_.store_);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  auto config = config_or.value();

  testing::NiceMock<Server::MockListenerManager> listener_manager;
  config->setListenerManager(listener_manager);
  // Enabling replays the existing provider, so the config hooks its update/remove callbacks.
  ASSERT_TRUE(config->enableSecretLifecycle());

  // Start the subscription, then deliver a delta SDS removal of the secret. The provider's remove
  // callback fires and reaches the module's on_secret_removal hook (a no-op module here, so this
  // exercises the path for coverage and proves it does not crash).
  init_target_handle->initialize(init_watcher);
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  *removed_resources.Add() = "secret_0";
  EXPECT_TRUE(secret_context.server_context_.cluster_manager_.subscription_factory_.callbacks_
                  ->onConfigUpdate({}, removed_resources, "")
                  .ok());
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
