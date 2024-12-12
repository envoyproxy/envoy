#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/filter_state_dst_address.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/clusters/original_dst/original_dst_cluster.h"
#include "source/extensions/common/wasm/ext/set_envoy_filter_state.pb.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"
#include "test/test_common/wasm_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class TestContext : public Context {};

class ForeignTest : public testing::Test {
public:
  ForeignTest() = default;

  void initializeFilterCallbacks() { ctx_.initializeReadFilterCallbacks(read_filter_callbacks_); }

protected:
  TestContext ctx_;
  testing::NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
};

TEST_F(ForeignTest, ForeignFunctionEdgeCaseTest) {
#ifndef WASM_USE_CEL_PARSER
  GTEST_SKIP() << "Skipping the test because the CEL parser is disabled";
#endif
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info;

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  Wasm wasm(plugin->wasmConfig(), "", scope, *api, cluster_manager, *dispatcher);
  proxy_wasm::current_context_ = &ctx_;

  auto function = proxy_wasm::getForeignFunction("expr_evaluate");
  ASSERT_NE(function, nullptr);
  auto result = function(wasm, "", [](size_t size) { return malloc(size); });
  EXPECT_EQ(result, WasmResult::BadArgument);
  result = function(wasm, "\xff\xff\xff\xff", [](size_t size) { return malloc(size); });
  EXPECT_NE(result, WasmResult::Ok);

  function = proxy_wasm::getForeignFunction("expr_delete");
  ASSERT_NE(function, nullptr);
  result = function(wasm, "", [](size_t size) { return malloc(size); });
  EXPECT_EQ(result, WasmResult::BadArgument);
}

TEST_F(ForeignTest, ForeignFunctionSetEnvoyFilterTest) {
  initializeFilterCallbacks(); // so that we can test setting filter state
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info;

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  Wasm wasm(plugin->wasmConfig(), "", scope, *api, cluster_manager, *dispatcher);
  proxy_wasm::current_context_ = &ctx_;

  auto function = proxy_wasm::getForeignFunction("set_envoy_filter_state");
  ASSERT_NE(function, nullptr);

  auto result = function(wasm, "bad_arg", [](size_t size) { return malloc(size); });
  EXPECT_EQ(result, WasmResult::BadArgument);

  envoy::source::extensions::common::wasm::SetEnvoyFilterStateArguments args;
  std::string in;

  args.set_path("invalid.path");
  args.set_value("unicorns");
  args.SerializeToString(&in);
  result = function(wasm, in, [](size_t size) { return malloc(size); });
  EXPECT_EQ(result, WasmResult::NotFound);

  auto* stream_info = ctx_.getRequestStreamInfo();
  ASSERT_NE(stream_info, nullptr);

  args.set_path(TcpProxy::PerConnectionCluster::key());
  args.set_value("unicorns");
  args.set_span(envoy::source::extensions::common::wasm::LifeSpan::DownstreamRequest);
  args.SerializeToString(&in);
  result = function(wasm, in, [](size_t size) { return malloc(size); });
  EXPECT_EQ(result, WasmResult::Ok);
  EXPECT_TRUE(stream_info->filterState()->hasData<TcpProxy::PerConnectionCluster>(
      TcpProxy::PerConnectionCluster::key()));

  args.set_path(Upstream::OriginalDstClusterFilterStateKey);
  args.set_value("1.2.3.4:80");
  args.set_span(envoy::source::extensions::common::wasm::LifeSpan::DownstreamRequest);
  args.SerializeToString(&in);
  result = function(wasm, in, [](size_t size) { return malloc(size); });
  EXPECT_EQ(result, WasmResult::Ok);
  EXPECT_TRUE(stream_info->filterState()->hasData<Network::AddressObject>(
      Upstream::OriginalDstClusterFilterStateKey));
}

class StreamForeignTest : public WasmPluginConfigTestBase<
                              testing::TestWithParam<std::tuple<std::string, std::string>>> {
public:
  StreamForeignTest() = default;
};

INSTANTIATE_TEST_SUITE_P(PluginConfigRuntimes, StreamForeignTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(StreamForeignTest, ForeignFunctionClearRouteCache) {
  auto [runtime, language] = GetParam();

  auto plugin_config = getWasmPluginConfigForTest(
      runtime, "test/extensions/common/wasm/test_data/test_context_cpp.wasm",
      "CommonWasmTestContextCpp", "send local reply twice");

  setUp(plugin_config);

  createStreamContext();

  proxy_wasm::current_context_ = context_.get();
  auto function = proxy_wasm::getForeignFunction("clear_route_cache");

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  function(*plugin_config_->wasm(), "", [](size_t size) { return malloc(size); });
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
