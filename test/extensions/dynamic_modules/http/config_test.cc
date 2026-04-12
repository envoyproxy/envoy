#include <fstream>

#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/background_fetch_manager.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
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
  }

  NiceMock<Network::MockListenerInfo> listener_info_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
  Api::ApiPtr api_;
  envoy::config::core::v3::Metadata listener_metadata_;
  Init::ManagerImpl init_manager_{"init_manager"};
  Init::ExpectableWatcherImpl init_watcher_;

  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

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
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
}

TEST_F(DynamicModuleFilterConfigTest, InlineBytesRejected) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      local:
        inline_bytes: "AAAA"
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(),
              testing::HasSubstr("Only local file path or remote HTTP source is supported"));
}

TEST_F(DynamicModuleFilterConfigTest, NoModuleOrName) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(),
              testing::HasSubstr("Either 'name' or 'module' must be specified"));
}

TEST_F(DynamicModuleFilterConfigTest, RemoteSourceWithoutInitManagerThrows) {
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
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // The ServerFactoryContext path has no init manager, so remote sources should be rejected.
  DynamicModuleConfigFactory factory;
  EXPECT_THROW_WITH_REGEX(factory.createFilterFactoryFromProtoWithServerContext(
                              proto_config, "stats", context_.server_factory_context_),
                          EnvoyException, "Remote module sources require an init manager");
}

TEST_F(DynamicModuleFilterConfigTest, RemoteSourceRegistersInitTarget) {
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
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  // The init manager should not be initialized yet — the remote fetch is pending.
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Uninitialized);
}

TEST_F(DynamicModuleFilterConfigTest, RemoteSourceFetchFailureFailOpen) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: "abc123"
        retry_policy:
          num_retries: 0
    do_not_close: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  // Initialize the init manager to trigger the fetch. Cluster "cluster_1" is not set up
  // in the mock, so the fetch fails immediately. With num_retries=0 and allow_empty=true,
  // the callback receives empty string and the filter is not installed.
  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  // Verify fail-open: the factory callback should not install any filter.
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(testing::_)).Times(0);
  cb_or_error.value()(filter_callbacks);
}

TEST_F(DynamicModuleFilterConfigTest, RemoteSourceFetchSuccess) {
  // Read the test shared object to use as the remote module content.
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");
  std::ifstream input(module_path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  // Compute the SHA256 that RemoteDataFetcher will verify against.
  Buffer::OwnedImpl hash_buffer(module_bytes);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // Set up cluster and HTTP client to return the module bytes on fetch.
  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  // Initialize → triggers fetch → HTTP success → module loaded from bytes.
  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  // Verify the factory callback installs the filter.
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  NiceMock<Event::MockDispatcher> worker_dispatcher{"worker_0"};
  ON_CALL(filter_callbacks, dispatcher()).WillByDefault(ReturnRef(worker_dispatcher));
  EXPECT_CALL(filter_callbacks, addStreamFilter(testing::_));
  cb_or_error.value()(filter_callbacks);

  // Clean up the temp file.
  std::filesystem::path temp_path = Extensions::DynamicModules::moduleTempPath(sha256);
  std::filesystem::remove(temp_path);
}

// Remote fetch returns data that is not a valid shared object (invalid ELF).
// newDynamicModuleFromBytes fails, the error is logged, and the filter is not installed
// (fail-open).
TEST_F(DynamicModuleFilterConfigTest, RemoteSourceFetchSuccessInvalidModule) {
  const std::string garbage_bytes = "this is not a valid shared object";

  Buffer::OwnedImpl hash_buffer(garbage_bytes);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(garbage_bytes);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  // The module load failed, so the filter should not be installed (fail-open).
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(testing::_)).Times(0);
  cb_or_error.value()(filter_callbacks);
}

// Remote fetch returns a valid shared object that loads successfully, but the module is missing
// required HTTP filter symbols (e.g., envoy_dynamic_module_on_http_filter_config_new).
// buildFilterFactoryCallback fails, the error is logged, and the filter is not installed.
TEST_F(DynamicModuleFilterConfigTest, RemoteSourceFetchSuccessMissingFilterSymbols) {
  const std::string module_path =
      Extensions::DynamicModules::testSharedObjectPath("no_http_config_new", "c");
  std::ifstream input(module_path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  Buffer::OwnedImpl hash_buffer(module_bytes);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  // Module loaded but filter config creation failed, so the filter should not be installed.
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(testing::_)).Times(0);
  cb_or_error.value()(filter_callbacks);

  // Clean up the temp file.
  std::filesystem::path temp_path = Extensions::DynamicModules::moduleTempPath(sha256);
  std::filesystem::remove(temp_path);
}

// After a successful remote fetch, newDynamicModuleFromBytes writes the module to a
// deterministic path based on SHA256. A subsequent create with the same SHA256 should
// find the file on disk and load it without an init manager (no RemoteAsyncDataProvider).
TEST_F(DynamicModuleFilterConfigTest, RemoteCacheHitAfterFetch) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");
  std::ifstream input(module_path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  Buffer::OwnedImpl hash_buffer(module_bytes);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // Set up cluster and HTTP client to return the module bytes on fetch.
  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  // First call: remote fetch writes the module to disk.
  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);

  // Second call: the file exists on disk so it loads directly without an init manager.
  // A different factory instance also works since the cache is filesystem-based.
  DynamicModuleConfigFactory factory2;
  auto result2 =
      factory2.createFilterFactory(proto_config, "", context_.server_factory_context_, stats_scope_,
                                   /*init_manager=*/nullptr);
  EXPECT_TRUE(result2.ok()) << result2.status().message();

  // Verify the cache-loaded factory callback installs the filter.
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  NiceMock<Event::MockDispatcher> worker_dispatcher{"worker_0"};
  ON_CALL(filter_callbacks, dispatcher()).WillByDefault(ReturnRef(worker_dispatcher));
  EXPECT_CALL(filter_callbacks, addStreamFilter(testing::_));
  result2.value()(filter_callbacks);

  // Clean up.
  std::filesystem::path temp_path = Extensions::DynamicModules::moduleTempPath(sha256);
  std::filesystem::remove(temp_path);
}

// When nack_on_cache_miss is set but the module is already cached, loading succeeds normally.
TEST_F(DynamicModuleFilterConfigTest, NackModeCacheHit) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");
  std::ifstream input(module_path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  Buffer::OwnedImpl hash_buffer(module_bytes);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  // Pre-write the module to the cache path so the cache-hit branch fires.
  auto cached_path = Extensions::DynamicModules::moduleTempPath(sha256);
  std::filesystem::create_directories(cached_path.parent_path());
  std::filesystem::copy_file(module_path, cached_path,
                             std::filesystem::copy_options::overwrite_existing);

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
    nack_on_cache_miss: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();

  // Clean up.
  std::filesystem::remove(cached_path);
}

// A cache miss with nack_on_cache_miss rejects the config.
TEST_F(DynamicModuleFilterConfigTest, NackModeCacheMissReturnsError) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: "deadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678"
    do_not_close: true
    nack_on_cache_miss: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("not cached"));
}

// The background fetch triggered by NACK mode writes the module to disk, so a subsequent
// config push finds it cached and succeeds.
TEST_F(DynamicModuleFilterConfigTest, NackModeBackgroundFetchPopulatesCache) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");
  std::ifstream input(module_path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  Buffer::OwnedImpl hash_buffer(module_bytes);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
    nack_on_cache_miss: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // Set up cluster and HTTP client to return the module bytes on fetch.
  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  // First call: cache miss → NACK. The mock HTTP client fires synchronously,
  // so the background fetch writes the module to disk before we return.
  DynamicModuleConfigFactory factory;
  auto result1 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_FALSE(result1.ok());
  EXPECT_THAT(result1.status().message(), testing::HasSubstr("not cached"));

  auto cached_path = Extensions::DynamicModules::moduleTempPath(sha256);
  EXPECT_TRUE(std::filesystem::exists(cached_path));

  // Second call finds the cached file and succeeds.
  auto result2 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_TRUE(result2.ok()) << result2.status().message();

  // Clean up.
  std::filesystem::remove(cached_path);
}

// When the background fetch fails, no file lands on disk and the next config push
// cleans up the old state and starts a fresh fetch.
TEST_F(DynamicModuleFilterConfigTest, NackModeBackgroundFetchFailure) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: "deadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678"
    do_not_close: true
    nack_on_cache_miss: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // Cluster is not initialized, so the fetch fails immediately.
  DynamicModuleConfigFactory factory;
  auto result1 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_FALSE(result1.ok());
  EXPECT_THAT(result1.status().message(), testing::HasSubstr("not cached"));

  auto cached_path = Extensions::DynamicModules::moduleTempPath(
      "deadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678");
  EXPECT_FALSE(std::filesystem::exists(cached_path));

  // Second call cleans up the completed (failed) entry and starts a new fetch.
  auto result2 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_FALSE(result2.ok());
  EXPECT_THAT(result2.status().message(), testing::HasSubstr("not cached"));
}

// NACK mode works without an init manager (ECDS / per-route path).
TEST_F(DynamicModuleFilterConfigTest, NackModeWithoutInitManager) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: "deadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678"
    do_not_close: true
    nack_on_cache_miss: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                            stats_scope_, /*init_manager=*/nullptr);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("not cached"));
  EXPECT_THAT(result.status().message(), testing::Not(testing::HasSubstr("init manager")));
}

// When a fetch is still in-flight, a second config push for the same SHA256
// reuses the existing entry instead of starting a duplicate fetch.
TEST_F(DynamicModuleFilterConfigTest, NackModeInFlightDedup) {
  const std::string yaml = R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: "deadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678"
    do_not_close: true
    nack_on_cache_miss: true
  filter_name: "test_filter"
  )EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // Set up the cluster so the fetch starts but capture the callback without firing it.
  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* captured_cb = nullptr;
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            captured_cb = &callbacks;
            return &request;
          }));

  DynamicModuleConfigFactory factory;

  // First call: starts a background fetch.
  auto result1 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_FALSE(result1.ok());
  EXPECT_THAT(result1.status().message(), testing::HasSubstr("not cached"));
  EXPECT_NE(captured_cb, nullptr);

  // Second call while the fetch is still in-flight: no new send_ expected (WillOnce above
  // would fail if a second call happened).
  auto result2 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_FALSE(result2.ok());
  EXPECT_THAT(result2.status().message(), testing::HasSubstr("not cached"));

  // Clean up: erase the in-flight entry from the singleton so the fetcher is destroyed
  // while the mock request is still alive. This triggers cancel() on the mock.
  EXPECT_CALL(request, cancel());
  Extensions::DynamicModules::BackgroundFetchManager::singleton(
      context_.server_factory_context_.singletonManager())
      ->erase("deadbeef1234567890abcdef1234567890abcdef1234567890abcdef12345678");
}

// When a background fetch delivers bytes that pass SHA256 validation but are
// not a valid shared object, onSuccess writes the file to disk (no dlopen
// attempted in background). The next config push finds the cached file,
// attempts to load it, and returns a clear load error.
TEST_F(DynamicModuleFilterConfigTest, NackModeBackgroundFetchBadModule) {
  const std::string bad_data = "this is not a valid shared object";
  Buffer::OwnedImpl hash_buffer(bad_data);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
    nack_on_cache_miss: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(bad_data);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  DynamicModuleConfigFactory factory;
  auto result1 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_FALSE(result1.ok());
  EXPECT_THAT(result1.status().message(), testing::HasSubstr("not cached"));

  // The background fetch only writes bytes to disk (no dlopen), so the file persists.
  auto cached_path = Extensions::DynamicModules::moduleTempPath(sha256);
  EXPECT_TRUE(std::filesystem::exists(cached_path));

  // Next config push finds the cached file, tries to load it, and gets a load error.
  auto result2 = factory.createFilterFactory(proto_config, "", context_.server_factory_context_,
                                             stats_scope_, &init_manager_);
  EXPECT_FALSE(result2.ok());
  EXPECT_THAT(result2.status().message(),
              testing::HasSubstr("Cached remote module failed to load"));

  std::filesystem::remove(cached_path);
}

// When the cached temp file is deleted, the factory should detect the missing file
// and fall through to the remote fetch path.
TEST_F(DynamicModuleFilterConfigTest, RemoteCacheInvalidationOnMissingFile) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");
  std::ifstream input(module_path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string module_bytes((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  Buffer::OwnedImpl hash_buffer(module_bytes);
  const std::string sha256 =
      Hex::encode(Common::Crypto::UtilitySingleton::get().getSha256Digest(hash_buffer));

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    module:
      remote:
        http_uri:
          uri: https://example.com/module.so
          cluster: cluster_1
          timeout: 5s
        sha256: ")EOF",
                                                                    sha256, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // Set up cluster and HTTP client for the initial remote fetch.
  auto& cm = context_.server_factory_context_.cluster_manager_;
  cm.initializeThreadLocalClusters({"cluster_1"});
  NiceMock<Http::MockAsyncClientRequest> request(&cm.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm.thread_local_cluster_.async_client_, send_(testing::_, testing::_, testing::_))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body().add(module_bytes);
            callbacks.onSuccess(request, std::move(response));
            return nullptr;
          }));

  // First call: remote fetch writes the module to disk.
  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok());

  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);

  // Delete the temp file to simulate invalidation.
  std::filesystem::path temp_path = Extensions::DynamicModules::moduleTempPath(sha256);
  std::filesystem::remove(temp_path);
  ASSERT_FALSE(std::filesystem::exists(temp_path));

  // Second call with init_manager=nullptr: file is gone, so it needs an init manager.
  auto result2 =
      factory.createFilterFactory(proto_config, "", context_.server_factory_context_, stats_scope_,
                                  /*init_manager=*/nullptr);
  EXPECT_FALSE(result2.ok());
  EXPECT_THAT(result2.status().message(),
              testing::HasSubstr("Remote module sources require an init manager"));
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
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(), testing::HasSubstr("Failed to load dynamic module"));
}

// Verify that when both name and module are set, module takes precedence.
TEST_F(DynamicModuleFilterConfigTest, ModulePrecedenceOverName) {
  const std::string module_path = Extensions::DynamicModules::testSharedObjectPath("no_op", "c");

  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  dynamic_module_config:
    name: "nonexistent_module_should_be_ignored"
    module:
      local:
        filename: ")EOF",
                                                                    module_path, R"EOF("
    do_not_close: true
  filter_name: "test_filter"
  )EOF"));

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  DynamicModuleConfigFactory factory;
  // If name were used, this would fail because "nonexistent_module_should_be_ignored" doesn't
  // exist. Since module takes precedence, it should succeed with the local file.
  auto cb_or_error = factory.createFilterFactoryFromProto(proto_config, "stats", context_);
  EXPECT_TRUE(cb_or_error.ok()) << cb_or_error.status().message();
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
