#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/http/header_formatters/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/http/header_formatters/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::IsOk;

using ProtoConfig =
    envoy::extensions::http::header_formatters::dynamic_modules::v3::DynamicModuleHeaderFormatter;

// Builds a proto config that loads the named module with the given in-module formatter name.
ProtoConfig protoConfig(absl::string_view module_name, absl::string_view formatter_name) {
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
header_formatter_name: {}
)EOF",
                                       module_name, formatter_name);
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  return proto_config;
}

class DynamicModuleHeaderFormatterFactoryTest : public testing::Test {
public:
  DynamicModuleHeaderFormatterFactoryTest() {
    const std::string shared_object_dir =
        std::filesystem::path(
            Extensions::DynamicModules::testSharedObjectPath("header_formatter_no_op", "c"))
            .parent_path()
            .string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  absl::StatusOr<Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr>
  create(const ProtoConfig& proto_config) {
    return factory_.createFactoryFromProto(proto_config, context_);
  }

  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  DynamicModuleHeaderFormatterFactoryConfig factory_;
};

TEST_F(DynamicModuleHeaderFormatterFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.http.stateful_header_formatters.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, Category) {
  EXPECT_EQ("envoy.http.stateful_header_formatters", factory_.category());
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<ProtoConfig*>(proto.get()));
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, FactoryRegistration) {
  auto* registered =
      Registry::FactoryRegistry<Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig>::getFactory(
          "envoy.http.stateful_header_formatters.dynamic_modules");
  ASSERT_NE(nullptr, registered);
  EXPECT_EQ(factory_.name(), registered->name());
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, ValidConfig) {
  auto config = create(protoConfig("header_formatter_no_op", "test_formatter"));
  ASSERT_THAT(config.status(), IsOk());
  ASSERT_NE(nullptr, config.value());
  EXPECT_NE(nullptr, config.value()->create());
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, ValidConfigWithLocalFile) {
  const std::string path =
      Extensions::DynamicModules::testSharedObjectPath("header_formatter_no_op", "c");
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  module:
    local:
      filename: {}
  do_not_close: true
header_formatter_name: test_formatter
)EOF",
                                       path);
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  auto config = create(proto_config);
  ASSERT_THAT(config.status(), IsOk());
  EXPECT_NE(nullptr, config.value());
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, ValidConfigWithFormatterConfig) {
  auto proto_config = protoConfig("header_formatter_preserve_case", "test_formatter");
  Protobuf::StringValue value;
  value.set_value("proper_case");
  ASSERT_TRUE(proto_config.mutable_header_formatter_config()->PackFrom(value));
  auto config = create(proto_config);
  ASSERT_THAT(config.status(), IsOk());
  EXPECT_NE(nullptr, config.value());
}

// createFactoryFromProto() gets no init manager, so there is nothing to await an asynchronous
// remote fetch on and a remote source that is not already cached must be rejected outright.
TEST_F(DynamicModuleHeaderFormatterFactoryTest, RemoteSourceRejected) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  module:
    remote:
      http_uri:
        uri: https://example.com/libheader_formatter_no_op.so
        cluster: some_cluster
        timeout: 5s
      sha256: "0000000000000000000000000000000000000000000000000000000000000000"
header_formatter_name: test_formatter
)EOF";
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  EXPECT_FALSE(create(proto_config).ok());
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, InvalidModule) {
  auto config = create(protoConfig("nonexistent_module", "test_formatter"));
  ASSERT_FALSE(config.ok());
  EXPECT_THAT(std::string(config.status().message()), testing::HasSubstr("Failed to load"));
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, MissingSymbolsRejected) {
  for (const auto& [module, symbol] : std::vector<std::pair<std::string, std::string>>{
           {"header_formatter_missing_config_new",
            "envoy_dynamic_module_on_header_formatter_config_new"},
           {"header_formatter_missing_config_destroy",
            "envoy_dynamic_module_on_header_formatter_config_destroy"},
           {"header_formatter_missing_new", "envoy_dynamic_module_on_header_formatter_new"},
           {"header_formatter_missing_destroy", "envoy_dynamic_module_on_header_formatter_destroy"},
           {"header_formatter_missing_process_key",
            "envoy_dynamic_module_on_header_formatter_process_key"},
           {"header_formatter_missing_format",
            "envoy_dynamic_module_on_header_formatter_format"}}) {
    SCOPED_TRACE(module);
    auto config = create(protoConfig(module, "test_formatter"));
    ASSERT_FALSE(config.ok());
    EXPECT_TRUE(absl::IsNotFound(config.status()));
    EXPECT_THAT(std::string(config.status().message()), testing::HasSubstr(symbol));
  }
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, ConfigNewReturnsNull) {
  auto config = create(protoConfig("header_formatter_config_new_fail", "test_formatter"));
  ASSERT_FALSE(config.ok());
  EXPECT_TRUE(absl::IsInvalidArgument(config.status()));
  EXPECT_THAT(std::string(config.status().message()),
              testing::HasSubstr("Failed to initialize dynamic module header formatter config"));
}

TEST_F(DynamicModuleHeaderFormatterFactoryTest, MalformedFormatterConfig) {
  auto proto_config = protoConfig("header_formatter_no_op", "test_formatter");
  // A StringValue type URL with a payload that is not a valid StringValue.
  auto* any = proto_config.mutable_header_formatter_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("\xff\xff\xff\xff");
  auto config = create(proto_config);
  ASSERT_FALSE(config.ok());
  EXPECT_THAT(std::string(config.status().message()),
              testing::HasSubstr("Failed to parse header formatter config"));
}

// dynamic_module_config is required, so an empty config is rejected by proto validation. This one
// still throws rather than returning a status: the rejection comes from downcastAndValidate()
// before the factory gets a chance to report anything.
TEST_F(DynamicModuleHeaderFormatterFactoryTest, MissingDynamicModuleConfig) {
  ProtoConfig proto_config;
  // IgnoreError() only satisfies the nodiscard attribute; create() throws before it returns.
  EXPECT_THROW(create(proto_config).IgnoreError(), ProtoValidationException);
}

} // namespace
} // namespace DynamicModules
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
