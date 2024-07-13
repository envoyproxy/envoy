#include "source/extensions/router/cluster_specifiers/lua/lua_cluster_specifier.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"
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
  std::unique_ptr<LuaClusterSpecifierPlugin> plugin_;
  LuaClusterSpecifierConfigSharedPtr config_;
};

// Normal lua code test.
TEST_F(LuaClusterSpecifierPluginTest, NormalLuaCode) {
  setUpTest(normal_lua_config_yaml_);

  auto mock_route = std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "fake"}};
    auto route = plugin_->route(mock_route, headers);
    EXPECT_EQ("fake_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "header_value"}};
    auto route = plugin_->route(mock_route, headers);
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
    auto route = plugin_->route(mock_route, headers);
    EXPECT_EQ("default_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "header_value"}};
    auto route = plugin_->route(mock_route, headers);
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
    auto route = plugin_->route(mock_route, headers);
    EXPECT_EQ("fake_service", route->routeEntry()->clusterName());
    // Force the runtime to gc and destroy all the userdata.
    config_->perLuaCodeSetup()->runtimeGC();
  }

  {
    Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"header_key", "header_value"}};
    auto route = plugin_->route(mock_route, headers);
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

TEST_F(LuaClusterSpecifierPluginTest, DestructLuaClusterSpecifierConfigDisableRuntime) {
  TestScopedRuntime runtime;
  runtime.mergeValues({{"envoy.restart_features.allow_slot_destroy_on_worker_threads", "false"}});

  setUpTest(normal_lua_config_yaml_);
  InSequence s;
  EXPECT_CALL(server_factory_context_.dispatcher_, isThreadSafe()).WillOnce(Return(false));
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_));
  EXPECT_CALL(server_factory_context_.dispatcher_, isThreadSafe()).WillOnce(Return(true));
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_)).Times(0);

  config_.reset();
  plugin_.reset();

  LuaClusterSpecifierConfigProto proto_config{};
  TestUtility::loadFromYaml(normal_lua_config_yaml_, proto_config);
  config_ = std::make_shared<LuaClusterSpecifierConfig>(proto_config, server_factory_context_);
  config_.reset();
}

} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
