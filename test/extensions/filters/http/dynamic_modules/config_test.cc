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
#include "source/extensions/dynamic_modules/module_cache.h"
#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
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

  void SetUp() override { Extensions::DynamicModules::clearModuleCacheForTesting(); }

  NiceMock<Network::MockListenerInfo> listener_info_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
  Api::ApiPtr api_;
  envoy::config::core::v3::Metadata listener_metadata_;
  Init::ManagerImpl init_manager_{"init_manager"};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  NiceMock<Event::MockDispatcher> dispatcher_;

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

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingNackFetchFailure) {
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
            callbacks.onFailure(request, Http::AsyncClient::FailureReason::Reset);
            return &request;
          }));

  DynamicModuleConfigFactory factory;

  // First attempt NACKs; the background fetch fails, creating a negative cache entry.
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  // Second attempt hits the negative cache, no new fetch.
  Init::ManagerImpl init_manager2{"init_manager2"};
  Init::ExpectableWatcherImpl init_watcher2;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager2));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("negative cache hit"));

  EXPECT_CALL(init_watcher2, ready());
  init_manager2.initialize(init_watcher2);
  EXPECT_EQ(init_manager2.state(), Init::Manager::State::Initialized);

  dispatcher_.clearDeferredDeleteList();
}

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingNackFetchInProgress) {
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
      .WillRepeatedly(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

  Http::AsyncClient::Callbacks* async_callbacks = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            async_callbacks = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;

  // NACK; the background fetch has started but hasn't completed yet.
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);

  // Second attempt sees the in-progress fetch.
  Init::ManagerImpl init_manager2{"init_manager2"};
  Init::ExpectableWatcherImpl init_watcher2;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager2));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("fetch in progress"));

  EXPECT_CALL(init_watcher2, ready());
  init_manager2.initialize(init_watcher2);

  // Clearing the deferred delete list here simulates what the real event loop does at the end
  // of each iteration. The shared_ptr<unique_ptr<>> holder pattern must keep the adapter+fetcher
  // alive through this; without it, the fetcher would be destroyed and async_callbacks would
  // become a dangling pointer.
  dispatcher_.clearDeferredDeleteList();

  // Now let the background fetch complete.
  ASSERT_NE(async_callbacks, nullptr);
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response->body().add(module_bytes);
  async_callbacks->onSuccess(request, std::move(response));

  // Third attempt should find the module in cache.
  Init::ManagerImpl init_manager3{"init_manager3"};
  Init::ExpectableWatcherImpl init_watcher3;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager3));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher3, ready());
  init_manager3.initialize(init_watcher3);
  EXPECT_EQ(init_manager3.state(), Init::Manager::State::Initialized);

  dispatcher_.clearDeferredDeleteList();
}

// Exercises the full NACK-mode cache lifecycle: fail, negative-cache, expire, succeed, expire.
TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingNackCacheLifecycle) {
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
      .WillRepeatedly(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

  int send_count = 0;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillRepeatedly(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            send_count++;
            if (send_count == 1) {
              callbacks.onFailure(request, Http::AsyncClient::FailureReason::Reset);
            } else {
              Http::ResponseMessagePtr response(
                  new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                      new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
              response->body().add(module_bytes);
              callbacks.onSuccess(request, std::move(response));
            }
            return &request;
          }));

  DynamicModuleConfigFactory factory;

  // First attempt: NACK, background fetch fails, lands in negative cache.
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);

  // Negative cache hit.
  Init::ManagerImpl init_manager2{"init_manager2"};
  Init::ExpectableWatcherImpl init_watcher2;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager2));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("negative cache hit"));

  EXPECT_CALL(init_watcher2, ready());
  init_manager2.initialize(init_watcher2);

  // Advance past the 10s negative cache TTL.
  Extensions::DynamicModules::setTimeOffsetForModuleCacheForTesting(std::chrono::seconds(11));

  // Negative cache expired, re-fetch succeeds but still NACKs (always NACK on first load).
  Init::ManagerImpl init_manager3{"init_manager3"};
  Init::ExpectableWatcherImpl init_watcher3;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager3));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);

  EXPECT_CALL(init_watcher3, ready());
  init_manager3.initialize(init_watcher3);

  // Cache hit, module loads successfully.
  Init::ManagerImpl init_manager4{"init_manager4"};
  Init::ExpectableWatcherImpl init_watcher4;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager4));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher4, ready());
  init_manager4.initialize(init_watcher4);
  EXPECT_EQ(init_manager4.state(), Init::Manager::State::Initialized);

  // Advance past the 24h positive cache TTL.
  Extensions::DynamicModules::setTimeOffsetForModuleCacheForTesting(
      std::chrono::seconds(11 + 24 * 3600 + 1));

  // Positive cache expired, back to NACK.
  Init::ManagerImpl init_manager5{"init_manager5"};
  Init::ExpectableWatcherImpl init_watcher5;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager5));

  cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().code(), absl::StatusCode::kUnavailable);

  EXPECT_CALL(init_watcher5, ready());
  init_manager5.initialize(init_watcher5);

  dispatcher_.clearDeferredDeleteList();
}

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingWarmingModeSuccess) {
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
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initialized);
}

TEST_F(DynamicModuleFilterConfigTest, RemoteLoadingWarmingModeFetchFailure) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  // Set num_retries: 0 so RemoteAsyncDataProvider won't try to use the retry timer.
  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        retry_policy:
          num_retries: 0
        sha256: )EOF",
                                        sha256, R"EOF(
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
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
            callbacks.onSuccess(request, std::move(response));
            return &request;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  init_manager_.initialize(init_watcher_);
  EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initialized);

  // Fetch failed so the callback is a no-op (fail-open).
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_)).Times(0);
  cb_or_error.value()(filter_callback);
}

TEST_F(DynamicModuleFilterConfigTest, EmptyLocalModuleData) {
  const std::string empty_file = TestEnvironment::temporaryPath("empty_module.so");
  { std::ofstream f(empty_file); }

  const std::string yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      local:
        filename: ")EOF",
                                        empty_file, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("Module data is empty"));
}

TEST_F(DynamicModuleFilterConfigTest, ServerContextFactory) {
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
  EXPECT_FALSE(
      factory.isTerminalFilterByProtoTyped(proto_config, context_.server_factory_context_));
  EXPECT_NO_THROW(factory.createFilterFactoryFromProtoWithServerContextTyped(
      proto_config, "stats", context_.server_factory_context_));

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(DynamicModuleFilterConfigTest, ServerContextRemoteNoInitManager) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: "abc123"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  EXPECT_THROW(factory.createFilterFactoryFromProtoWithServerContextTyped(
                   proto_config, "stats", context_.server_factory_context_),
               EnvoyException);
}

TEST_F(DynamicModuleFilterConfigTest, WarmingModeFetchInProgress) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  std::ifstream file(module_path, std::ios::binary);
  ASSERT_TRUE(file.good()) << "Failed to open test module: " << module_path;
  std::string module_bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  ASSERT_FALSE(module_bytes.empty());

  const std::string sha256 = Hex::encode(
      Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(module_bytes)));

  // First: NACK mode starts a background fetch that stays in-progress.
  const std::string nack_yaml = absl::StrCat(R"EOF(
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

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter nack_proto;
  TestUtility::loadFromYaml(nack_yaml, nack_proto);

  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillRepeatedly(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

  Http::AsyncClient::Callbacks* async_callbacks = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            async_callbacks = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;

  auto cb_or_error = factory.createFilterFactoryFromProto(nack_proto, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);

  // Second: warming mode (nack_on_module_cache_miss defaults to false) sees in-progress fetch.
  const std::string warming_yaml = absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: )EOF",
                                                sha256, R"EOF(
    do_not_close: true
  filter_name: "test_filter"
  )EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter warming_proto;
  TestUtility::loadFromYaml(warming_yaml, warming_proto);

  Init::ManagerImpl init_manager2{"init_manager2"};
  Init::ExpectableWatcherImpl init_watcher2;
  EXPECT_CALL(context_, initManager()).WillRepeatedly(ReturnRef(init_manager2));

  cb_or_error = factory.createFilterFactoryFromProto(warming_proto, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_EQ(cb_or_error.status().message(), "Module fetch in progress");

  EXPECT_CALL(init_watcher2, ready());
  init_manager2.initialize(init_watcher2);

  // Complete the background fetch to clean up.
  ASSERT_NE(async_callbacks, nullptr);
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response->body().add(module_bytes);
  async_callbacks->onSuccess(request, std::move(response));

  dispatcher_.clearDeferredDeleteList();
}

TEST_F(DynamicModuleFilterConfigTest, RouteSpecificConfigPerRouteConfigFail) {
  // Set up the search path to find the test module.
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  // http_filter_per_route_config_new_fail exports the per-route config symbol but returns nullptr.
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: "http_filter_per_route_config_new_fail"
    do_not_close: true
  per_route_config_name: "test"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> visitor;
  auto config_or_error = factory.createRouteSpecificFilterConfig(
      proto_config, context_.server_factory_context_, visitor);
  EXPECT_FALSE(config_or_error.ok());
  EXPECT_THAT(config_or_error.status().message(),
              testing::HasSubstr("Failed to create pre-route filter config"));

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(DynamicModuleFilterConfigTest, RouteSpecificConfigInvalidModule) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    name: "nonexistent_module"
    do_not_close: true
  per_route_config_name: "test"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  NiceMock<ProtobufMessage::MockValidationVisitor> visitor;
  auto config_or_error = factory.createRouteSpecificFilterConfig(
      proto_config, context_.server_factory_context_, visitor);
  EXPECT_FALSE(config_or_error.ok());
  EXPECT_THAT(config_or_error.status().message(),
              testing::HasSubstr("Failed to load dynamic module"));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
