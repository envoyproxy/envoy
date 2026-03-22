#include "envoy/registry/registry.h"

#include "source/extensions/access_loggers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {
namespace {

class DynamicModuleAccessLogFactoryTest : public testing::Test {
public:
  DynamicModuleAccessLogFactoryTest() {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("access_log_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  DynamicModuleAccessLogFactory factory_;
};

TEST_F(DynamicModuleAccessLogFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.access_loggers.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleAccessLogFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(
      nullptr,
      dynamic_cast<envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog*>(
          proto.get()));
}

TEST_F(DynamicModuleAccessLogFactoryTest, ValidConfig) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_no_op
  do_not_close: true
logger_name: test_logger
logger_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  auto access_log = factory_.createAccessLogInstance(proto_config, std::move(filter), context, {});
  EXPECT_NE(nullptr, access_log);
}

TEST_F(DynamicModuleAccessLogFactoryTest, ValidConfigWithFilter) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_no_op
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = std::make_unique<NiceMock<AccessLog::MockFilter>>();
  auto access_log = factory_.createAccessLogInstance(proto_config, std::move(filter), context, {});
  EXPECT_NE(nullptr, access_log);
}

TEST_F(DynamicModuleAccessLogFactoryTest, InvalidModule) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: nonexistent_module
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to load.*");
}

TEST_F(DynamicModuleAccessLogFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
      "envoy.access_loggers.dynamic_modules");
  EXPECT_NE(nullptr, factory);
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingConfigNew) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_config_new
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*config_new");
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingConfigDestroy) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_config_destroy
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*config_destroy");
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingLoggerNew) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_logger_new
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*logger_new");
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingLoggerLog) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_logger_log
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*logger_log");
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingLoggerDestroy) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_logger_destroy
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*logger_destroy");
}

} // namespace
} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
