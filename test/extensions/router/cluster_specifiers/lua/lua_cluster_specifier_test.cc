#include "source/extensions/router/cluster_specifiers/lua/lua_cluster_specifier.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Lua {

using testing::InSequence;
using testing::NiceMock;
using testing::Return;

class LuaClusterSpecifierPluginTest : public testing::Test {
public:
  void setUpTest(const std::string& yaml) {
    LuaClusterSpecifierConfigProto proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);

    config_ = std::make_shared<LuaClusterSpecifierConfig>(proto_config, server_factory_context_);

    plugin_ = std::make_unique<LuaClusterSpecifierPlugin>(config_);
  }

  const std::string normal_lua_config_yaml_ = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local header_value = route_handle:headers():get("header_key")
        if header_value == "fake" then
          return "fake_service"
        end
        return "web_service"
      end
  default_cluster: default_service
  )EOF";

  const std::string error_lua_config_yaml_ = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local header_value = route_handle:headers():get({})
        if header_value == "fake" then
          return "fake_service"
        end
        return "web_service"
      end
  default_cluster: default_service
  )EOF";

  const std::string return_type_not_string_lua_config_yaml_ = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local header_value = route_handle:headers():get("header_key")
        if header_value == "fake" then
          return "fake_service"
        end
        return {}
      end
  default_cluster: default_service
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::unique_ptr<LuaClusterSpecifierPlugin> plugin_;
  LuaClusterSpecifierConfigSharedPtr config_;
};

// Normal lua code test.
TEST_F(LuaClusterSpecifierPluginTest, NormalLuaCode) {
  setUpTest(normal_lua_config_yaml_);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "fake"}};
    auto route = plugin_->route(mock_route, headers, stream_info_, 0);
    EXPECT_EQ("fake_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "header_value"}};
    auto route = plugin_->route(mock_route, headers, stream_info_, 0);
    EXPECT_EQ("web_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }
}

// Error lua code test.
TEST_F(LuaClusterSpecifierPluginTest, ErrorLuaCode) {
  setUpTest(error_lua_config_yaml_);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "fake"}};
    auto route = plugin_->route(mock_route, headers, stream_info_, 0);
    EXPECT_EQ("default_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "header_value"}};
    auto route = plugin_->route(mock_route, headers, stream_info_, 0);
    EXPECT_EQ("default_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }
}

// Return type not string lua code test.
TEST_F(LuaClusterSpecifierPluginTest, ReturnTypeNotStringLuaCode) {
  setUpTest(return_type_not_string_lua_config_yaml_);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "fake"}};
    auto route = plugin_->route(mock_route, headers, stream_info_, 0);
    EXPECT_EQ("fake_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "header_value"}};
    auto route = plugin_->route(mock_route, headers, stream_info_, 0);
    EXPECT_EQ("default_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }
}

TEST_F(LuaClusterSpecifierPluginTest, DestructLuaClusterSpecifierConfig) {
  setUpTest(normal_lua_config_yaml_);
  InSequence s;
  EXPECT_CALL(server_factory_context_.dispatcher_, isThreadSafe()).Times(0);
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_)).Times(0);

  config_.reset();
  plugin_.reset();

  LuaClusterSpecifierConfigProto proto_config{};
  TestUtility::loadFromYaml(normal_lua_config_yaml_, proto_config);
  config_ = std::make_shared<LuaClusterSpecifierConfig>(proto_config, server_factory_context_);
  config_.reset();
}

TEST_F(LuaClusterSpecifierPluginTest, GetClustersBadArg) {
  const std::string config = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local wrong_type = true
        route_handle:getCluster(wrong_type)
        return "completed"
      end
  default_cluster: default_service
  )EOF";
  setUpTest(config);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("default_service", route->routeEntry()->clusterName());
}

TEST_F(LuaClusterSpecifierPluginTest, GetClustersMissing) {
  const std::string config = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local result = route_handle:getCluster("not found cluster name")
        if result == nil then
          return "nil"
        end
        return "not-nil"
      end
  default_cluster: default_service
  )EOF";
  setUpTest(config);

  EXPECT_CALL(server_factory_context_.cluster_manager_,
              getThreadLocalCluster(absl::string_view("not found cluster name")))
      .WillOnce(Return(nullptr));

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("nil", route->routeEntry()->clusterName());
}

TEST_F(LuaClusterSpecifierPluginTest, ClusterMethods) {
  const std::string config = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local result = route_handle:getCluster("my_cluster")
        if result == nil then
          return "fail: nil cluster"
        end
        if result:numConnections() ~= 2 then
          return "fail: wrong num connections"
        end
        if result:numRequests() ~= 4 then
          return "fail: wrong num requests"
        end
        if result:numPendingRequests() ~= 6 then
          return "fail: wrong num pending requests"
        end
        return "pass"
      end
  default_cluster: default_service
  )EOF";
  setUpTest(config);

  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  EXPECT_CALL(server_factory_context_.cluster_manager_,
              getThreadLocalCluster(absl::string_view("my_cluster")))
      .WillOnce(Return(&cluster));

  // The mock object returns the same resource manager for both the default and high priority,
  // so the counts in this test seen by lua are all doubled.

  // 1 connection.
  cluster.cluster_.info_->resource_manager_->connections().inc();

  // 2 requests.
  cluster.cluster_.info_->resource_manager_->requests().inc();
  cluster.cluster_.info_->resource_manager_->requests().inc();

  // 3 pending requests.
  cluster.cluster_.info_->resource_manager_->pendingRequests().inc();
  cluster.cluster_.info_->resource_manager_->pendingRequests().inc();
  cluster.cluster_.info_->resource_manager_->pendingRequests().inc();

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("pass", route->routeEntry()->clusterName());

  // Force the runtime to gc and destroy all the userdata.
  config_->perLuaCodeSetup()->runtimeGC();
}

TEST_F(LuaClusterSpecifierPluginTest, ClusterRef) {
  const std::string config = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local c1 = route_handle:getCluster("cluster1")
        local c2 = route_handle:getCluster("cluster2")
        local c3 = route_handle:getCluster("cluster3")
        local c4 = route_handle:getCluster("cluster3")
        local val1 = c1:numRequests()
        local val2 = c2:numRequests()
        local val3 = c3:numRequests()
        local val4 = c4:numRequests()
        if val1 == 2 and val2 == 2 and val3 == 2 and val4 == 2 then
          return "pass"
        end
        return "fail"
      end
  default_cluster: default_service
  )EOF";
  setUpTest(config);

  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  EXPECT_CALL(server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .Times(4)
      .WillRepeatedly(Return(&cluster));
  cluster.cluster_.info_->resource_manager_->requests().inc();

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  auto route = plugin_->route(mock_route, headers, stream_info_, 0);
  EXPECT_EQ("pass", route->routeEntry()->clusterName());

  // Force the runtime to gc and destroy all the userdata.
  config_->perLuaCodeSetup()->runtimeGC();
}

TEST_F(LuaClusterSpecifierPluginTest, Logging) {
  const std::string config = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        route_handle:logTrace("log test")
        route_handle:logDebug("log test")
        route_handle:logInfo("log test")
        route_handle:logWarn("log test")
        route_handle:logErr("log test")
        route_handle:logCritical("log test")
      end
  default_cluster: default_service
  )EOF";
  setUpTest(config);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  Http::TestRequestHeaderMapImpl headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({{"trace", "log test"},
                                                         {"debug", "log test"},
                                                         {"info", "log test"},
                                                         {"warn", "log test"},
                                                         {"error", "log test"},
                                                         {"critical", "log test"}}),
                             { plugin_->route(mock_route, headers, stream_info_, 0); });
}

} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
