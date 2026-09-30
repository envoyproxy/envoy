#include <filesystem>

#include "envoy/common/exception.h"
#include "envoy/extensions/http/early_header_mutation/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/http/early_header_mutation/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {
namespace {

using ::Envoy::Extensions::DynamicModules::failureCounter;
using ProtoConfig = envoy::extensions::http::early_header_mutation::dynamic_modules::v3::
    DynamicModuleEarlyHeaderMutation;

// Builds a proto config that loads the named module with the given in-module mutation name.
ProtoConfig protoConfig(absl::string_view module_name, absl::string_view mutation_name) {
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
early_header_mutation_name: {}
)EOF",
                                       module_name, mutation_name);
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  return proto_config;
}

// The factory receives the raw typed_config Any from HttpConnectionManagerConfig, so every call
// must go through an Any rather than the concrete message.
Protobuf::Any toAny(const Protobuf::Message& message) {
  Protobuf::Any any;
  EXPECT_TRUE(any.PackFrom(message));
  return any;
}

class DynamicModuleEarlyHeaderMutationFactoryTest : public testing::Test {
public:
  DynamicModuleEarlyHeaderMutationFactoryTest() {
    const std::string shared_object_dir =
        std::filesystem::path(
            Extensions::DynamicModules::testSharedObjectPath("early_header_mutation_no_op", "c"))
            .parent_path()
            .string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  Stats::Scope& serverScope() { return context_.server_factory_context_.serverScope(); }

  Envoy::Http::EarlyHeaderMutationPtr create(const ProtoConfig& proto_config) {
    return createFromAny(toAny(proto_config));
  }

  Envoy::Http::EarlyHeaderMutationPtr createFromAny(const Protobuf::Any& any) {
    return factory_.createExtension(any, context_);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Factory factory_;
};

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.http.early_header_mutation.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, Category) {
  EXPECT_EQ("envoy.http.early_header_mutation", factory_.category());
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<ProtoConfig*>(proto.get()));
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, FactoryRegistration) {
  auto* registered = Registry::FactoryRegistry<Envoy::Http::EarlyHeaderMutationFactory>::getFactory(
      "envoy.http.early_header_mutation.dynamic_modules");
  ASSERT_NE(nullptr, registered);
  EXPECT_EQ(factory_.name(), registered->name());
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, ValidConfig) {
  auto extension = create(protoConfig("early_header_mutation_no_op", "test_mutation"));
  EXPECT_NE(nullptr, extension);
  EXPECT_EQ(0, failureCounter(serverScope(), Extensions::DynamicModules::ModuleLoadErrorStat,
                              "test_mutation"));
  EXPECT_EQ(0, failureCounter(serverScope(), Extensions::DynamicModules::ConfigInitErrorStat,
                              "test_mutation"));
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, ValidConfigWithLocalFile) {
  const std::string path =
      Extensions::DynamicModules::testSharedObjectPath("early_header_mutation_no_op", "c");
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  module:
    local:
      filename: {}
  do_not_close: true
early_header_mutation_name: test_mutation
)EOF",
                                       path);
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  EXPECT_NE(nullptr, create(proto_config));
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, ValidConfigWithMutationConfig) {
  auto proto_config = protoConfig("early_header_mutation_rewrite", "test_mutation");
  Protobuf::StringValue value;
  value.set_value("x-tenant-id");
  ASSERT_TRUE(proto_config.mutable_early_header_mutation_config()->PackFrom(value));
  EXPECT_NE(nullptr, create(proto_config));
}

// No init manager is passed to newDynamicModuleByConfig, so a remote source that is not already
// cached cannot be awaited and must be rejected outright.
TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, RemoteSourceRejected) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  module:
    remote:
      http_uri:
        uri: https://example.com/libearly_header_mutation_no_op.so
        cluster: some_cluster
        timeout: 5s
      sha256: "0000000000000000000000000000000000000000000000000000000000000000"
early_header_mutation_name: test_mutation
)EOF";
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  EXPECT_THROW(create(proto_config), EnvoyException);
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, InvalidModule) {
  EXPECT_THROW_WITH_REGEX(create(protoConfig("nonexistent_module", "test_mutation")),
                          EnvoyException, "Failed to load");
  EXPECT_EQ(1, failureCounter(serverScope(), Extensions::DynamicModules::ModuleLoadErrorStat,
                              "test_mutation"));
}

// A missing ABI symbol is a module-level problem, so it is reported as module_load_error rather
// than config_init_error.
TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, MissingConfigNew) {
  EXPECT_THROW_WITH_REGEX(
      create(protoConfig("early_header_mutation_missing_config_new", "test_mutation")),
      EnvoyException, "envoy_dynamic_module_on_early_header_mutation_config_new");
  EXPECT_EQ(1, failureCounter(serverScope(), Extensions::DynamicModules::ModuleLoadErrorStat,
                              "test_mutation"));
  EXPECT_EQ(0, failureCounter(serverScope(), Extensions::DynamicModules::ConfigInitErrorStat,
                              "test_mutation"));
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, MissingConfigDestroy) {
  EXPECT_THROW_WITH_REGEX(
      create(protoConfig("early_header_mutation_missing_config_destroy", "test_mutation")),
      EnvoyException, "envoy_dynamic_module_on_early_header_mutation_config_destroy");
  EXPECT_EQ(1, failureCounter(serverScope(), Extensions::DynamicModules::ModuleLoadErrorStat,
                              "test_mutation"));
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, MissingMutate) {
  EXPECT_THROW_WITH_REGEX(
      create(protoConfig("early_header_mutation_missing_mutate", "test_mutation")), EnvoyException,
      "envoy_dynamic_module_on_early_header_mutation_mutate");
  EXPECT_EQ(1, failureCounter(serverScope(), Extensions::DynamicModules::ModuleLoadErrorStat,
                              "test_mutation"));
}

// The module loaded and every symbol resolved, but in-module initialization failed, so this is a
// config_init_error and not a module_load_error.
TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, ConfigNewReturnsNull) {
  EXPECT_THROW_WITH_REGEX(
      create(protoConfig("early_header_mutation_config_new_fail", "test_mutation")), EnvoyException,
      "Failed to initialize dynamic module early header mutation config");
  EXPECT_EQ(1, failureCounter(serverScope(), Extensions::DynamicModules::ConfigInitErrorStat,
                              "test_mutation"));
  EXPECT_EQ(0, failureCounter(serverScope(), Extensions::DynamicModules::ModuleLoadErrorStat,
                              "test_mutation"));
}

TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, MalformedMutationConfig) {
  auto proto_config = protoConfig("early_header_mutation_no_op", "test_mutation");
  // A StringValue type URL with a payload that is not a valid StringValue.
  auto* any = proto_config.mutable_early_header_mutation_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("\xff\xff\xff\xff");
  EXPECT_THROW_WITH_REGEX(create(proto_config), EnvoyException,
                          "Failed to parse early header mutation config");
  EXPECT_EQ(1, failureCounter(serverScope(), Extensions::DynamicModules::ConfigInitErrorStat,
                              "test_mutation"));
}

// Unlike the formatter and matcher factories, createExtension receives the raw typed_config Any,
// so a wrong type URL must be rejected by the unpacking step rather than crashing a downcast.
TEST_F(DynamicModuleEarlyHeaderMutationFactoryTest, WrongTypeUrlRejected) {
  Protobuf::StringValue unrelated;
  unrelated.set_value("not a config");
  EXPECT_THROW(createFromAny(toAny(unrelated)), EnvoyException);
}

} // namespace
} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
