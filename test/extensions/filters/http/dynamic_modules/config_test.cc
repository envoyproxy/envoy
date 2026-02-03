#include <chrono>
#include <fstream>

#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/code_cache.h"
#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace Configuration {

class DynamicModuleFilterConfigTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  DynamicModuleFilterConfigTest() : api_(Api::createApiForTest(stats_store_)) {
    ON_CALL(context_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(stats_scope_));
    ON_CALL(context_, listenerInfo()).WillByDefault(ReturnRef(listener_info_));
    ON_CALL(listener_info_, metadata()).WillByDefault(ReturnRef(listener_metadata_));
    EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager_));
    ON_CALL(context_.server_factory_context_, clusterManager())
        .WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(context_.server_factory_context_, mainThreadDispatcher())
        .WillByDefault(ReturnRef(dispatcher_));
  }

  void SetUp() override { Extensions::DynamicModules::clearCodeCacheForTesting(); }

  void initializeForRemote() {
    retry_timer_ = new Event::MockTimer();

    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillOnce(testing::Invoke([this](Event::TimerCb timer_cb) {
          retry_timer_cb_ = timer_cb;
          return retry_timer_;
        }));
  }

  NiceMock<Network::MockListenerInfo> listener_info_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
  Api::ApiPtr api_;
  envoy::config::core::v3::Metadata listener_metadata_;
  Init::ManagerImpl init_manager_{"init_manager"};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;

  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(DynamicModuleFilterConfigTest, LegacyNameBasedLoading) {
  // Set up the search path to find the test module.
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: "no_op"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(DynamicModuleFilterConfigTest, LocalFileLoading) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      local:
        filename: ")EOF",
                                                                    module_path, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
}

TEST_F(DynamicModuleFilterConfigTest, InlineBytesLoading) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      local:
        inline_bytes: ")EOF",
                                        Base64::encode(module_bytes.data(), module_bytes.size()),
                                        R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
}

// Remote loading tests are covered by RemoteLoadingNackOnCacheMiss which tests the
// fetch and cache mechanism. The warming mode path is complex due to stats lifecycle
// issues and is not tested here. Integration tests should cover the full flow.

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingNackOnCacheMiss) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                        sha256, R"EOF(
    nack_on_module_cache_miss: true
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  // First attempt should fail with NACK.
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  // Second attempt should succeed from cache.
  Init::ManagerImpl init_manager2{"init_manager2"};
  Init::ExpectableWatcherImpl init_watcher2;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager2));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher2, ready());
  init_manager2.initialize(init_watcher2);
  EXPECT_EQ(init_manager2.state(), Init::Manager::State::Initialized);

  dispatcher_.clearDeferredDeleteList();
}

// Note: RemoteMissingSha256 test is not included because the proto validation
// already enforces that sha256 must be non-empty for remote sources. The validation
// happens during config parsing, not in our factory code.

TEST_F(DynamicModuleFilterConfigTest, NoModuleOrName) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(),
              testing::HasSubstr("Either 'name' or 'module' must be specified"));
}

TEST_F(DynamicModuleFilterConfigTest, InvalidLocalFile) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      local:
        filename: "/nonexistent/path/to/module.so"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("Failed to read module data"));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
