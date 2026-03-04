#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
              testing::HasSubstr("Only local file path is supported"));
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

TEST_F(DynamicModuleFilterConfigTest, RemoteSourceRejected) {
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
  EXPECT_FALSE(cb_or_error.ok());
  EXPECT_THAT(cb_or_error.status().message(),
              testing::HasSubstr("Only local file path is supported"));
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
