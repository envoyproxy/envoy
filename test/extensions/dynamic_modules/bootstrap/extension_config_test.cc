#include "envoy/config/core/v3/config_source.pb.h"

#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
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

TEST_F(ExtensionConfigTest, EnableSecretLifecycleRequiresServerInitialized) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();
  auto config_or = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", DefaultMetricsNamespace, std::move(dynamic_module.value()), dispatcher_,
      context_, context_.store_);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  auto config = config_or.value();

  // Enabling is refused before the server is initialized.
  EXPECT_FALSE(config->enableSecretLifecycle());

  testing::NiceMock<Server::MockListenerManager> listener_manager;
  config->setListenerManager(listener_manager);
  // After initialization it can be enabled once; a second call is a no-op.
  EXPECT_TRUE(config->enableSecretLifecycle());
  EXPECT_FALSE(config->enableSecretLifecycle());
}

std::vector<std::string>& recordedSecretEvents() {
  static std::vector<std::string> events;
  return events;
}

void recordSecretAddOrUpdate(envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
                             envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
                             envoy_dynamic_module_type_envoy_buffer secret_name) {
  recordedSecretEvents().push_back(
      absl::StrCat("add_or_update:", absl::string_view(secret_name.ptr, secret_name.length)));
}

void recordSecretRemoval(envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
                         envoy_dynamic_module_type_bootstrap_extension_config_module_ptr,
                         envoy_dynamic_module_type_envoy_buffer secret_name) {
  recordedSecretEvents().push_back(
      absl::StrCat("removal:", absl::string_view(secret_name.ptr, secret_name.length)));
}

// Drives a dynamic TLS certificate provider in the real SecretManager behind the mock context and
// records the module hooks through their function pointers.
class SecretLifecycleExtensionConfigTest : public ExtensionConfigTest {
protected:
  SecretLifecycleExtensionConfigTest() : api_(Api::createApiForTest()) {
    recordedSecretEvents().clear();
    provider_dispatcher_ = api_->allocateDispatcher("test_thread");
    ON_CALL(init_manager_, add(testing::_))
        .WillByDefault(testing::Invoke([this](const Init::Target& target) {
          init_target_handle_ = target.createHandle("test");
          init_target_handle_->initialize(init_watcher_);
        }));
    ON_CALL(secret_context_.server_context_, mainThreadDispatcher())
        .WillByDefault(testing::ReturnRef(*provider_dispatcher_));
    ON_CALL(secret_context_.server_context_, localInfo())
        .WillByDefault(testing::ReturnRef(local_info_));
    ON_CALL(secret_context_.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  }

  DynamicModuleBootstrapExtensionConfigSharedPtr newConfig() {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        testDataDir() + "/libbootstrap_no_op.so", false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status();
    auto config = newDynamicModuleBootstrapExtensionConfig(
        "test", "config", DefaultMetricsNamespace, std::move(dynamic_module.value()), dispatcher_,
        context_, context_.store_);
    EXPECT_TRUE(config.ok()) << config.status();
    config.value()->on_bootstrap_extension_secret_add_or_update_ = &recordSecretAddOrUpdate;
    config.value()->on_bootstrap_extension_secret_removal_ = &recordSecretRemoval;
    config.value()->setListenerManager(listener_manager_);
    return config.value();
  }

  Secret::TlsCertificateConfigProviderSharedPtr createProvider() {
    auto provider = context_.secretManager().findOrCreateTlsCertificateProvider(
        config_source_, "secret_0", secret_context_.server_context_, init_manager_, true);
    subscription_ =
        secret_context_.server_context_.cluster_manager_.subscription_factory_.callbacks_;
    return provider;
  }

  void deliver(const std::string& cert) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name("secret_0");
    secret.mutable_tls_certificate()->mutable_certificate_chain()->set_inline_string(cert);
    secret.mutable_tls_certificate()->mutable_private_key()->set_inline_string("KEY");
    const auto decoded_resources = TestUtility::decodeResources({secret});
    EXPECT_TRUE(subscription_->onConfigUpdate(decoded_resources.refvec_, "").ok());
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr provider_dispatcher_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context_;
  envoy::config::core::v3::ConfigSource config_source_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  testing::NiceMock<Server::MockListenerManager> listener_manager_;
  Config::SubscriptionCallbacks* subscription_{};
};

// Enabling replays the delivered secret, rotation fires add-or-update, a delta xDS removal fires
// nothing, and the provider leaving the manager fires removal.
TEST_F(SecretLifecycleExtensionConfigTest, EventsReachModule) {
  auto provider = createProvider();
  deliver("CERT_A");
  auto config = newConfig();

  ASSERT_TRUE(config->enableSecretLifecycle());
  EXPECT_THAT(recordedSecretEvents(), testing::ElementsAre("add_or_update:secret_0"));

  deliver("CERT_B");
  EXPECT_THAT(recordedSecretEvents(),
              testing::ElementsAre("add_or_update:secret_0", "add_or_update:secret_0"));

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  *removed_resources.Add() = "secret_0";
  EXPECT_TRUE(subscription_->onConfigUpdate({}, removed_resources, "").ok());
  EXPECT_EQ(recordedSecretEvents().size(), 2U);

  provider.reset();
  EXPECT_THAT(
      recordedSecretEvents(),
      testing::ElementsAre("add_or_update:secret_0", "add_or_update:secret_0", "removal:secret_0"));
}

// No secret event reaches the module after ShutdownExit.
TEST_F(SecretLifecycleExtensionConfigTest, NoEventsAfterShutdownExit) {
  Server::ServerLifecycleNotifier::StageCallback shutdown_cb;
  EXPECT_CALL(context_.lifecycle_notifier_,
              registerCallback(Server::ServerLifecycleNotifier::Stage::ShutdownExit,
                               testing::An<Server::ServerLifecycleNotifier::StageCallback>()))
      .WillOnce(testing::DoAll(testing::SaveArg<1>(&shutdown_cb), testing::Return(nullptr)));
  auto config = newConfig();
  ASSERT_TRUE(config->enableSecretLifecycle());
  auto provider = createProvider();

  ASSERT_TRUE(shutdown_cb);
  shutdown_cb();
  deliver("CERT_A");
  provider.reset();
  EXPECT_TRUE(recordedSecretEvents().empty());
}

// Two extension configs observe the same secret independently.
TEST_F(SecretLifecycleExtensionConfigTest, TwoConfigsBothObserve) {
  auto first = newConfig();
  auto second = newConfig();
  ASSERT_TRUE(first->enableSecretLifecycle());
  ASSERT_TRUE(second->enableSecretLifecycle());
  auto provider = createProvider();
  deliver("CERT_A");
  EXPECT_THAT(recordedSecretEvents(),
              testing::ElementsAre("add_or_update:secret_0", "add_or_update:secret_0"));
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
