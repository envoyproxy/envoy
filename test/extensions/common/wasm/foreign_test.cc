#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class TestContext : public Context {};

class ForeignTest : public testing::Test {
protected:
  TestContext ctx_;
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

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
