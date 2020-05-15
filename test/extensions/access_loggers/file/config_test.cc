#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/registry/registry.h"

#include "common/access_log/access_log_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/file/config.h"
#include "extensions/access_loggers/file/file_access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {
namespace {

TEST(FileAccessLogConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(FileAccessLogFactory().createAccessLogInstance(
                   envoy::extensions::access_loggers::file::v3::FileAccessLog(), nullptr, context),
               ProtoValidationException);
}

TEST(FileAccessLogConfigTest, ConfigureFromProto) {
  envoy::config::accesslog::v3::AccessLog config;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Provided name for static registration lookup was empty.");

  config.set_name("INVALID");

  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Didn't find a registered implementation for name: 'INVALID'");

  envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
  fal_config.set_path("/dev/null");

  config.mutable_typed_config()->PackFrom(fal_config);

  config.set_name(AccessLogNames::get().File);

  AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(config, context);

  EXPECT_NE(nullptr, log);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));
}

TEST(FileAccessLogConfigTest, DEPRECATED_FEATURE_TEST(FileAccessLogTest)) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::AccessLogInstanceFactory>::getFactory(
          AccessLogNames::get().File);
  ASSERT_NE(nullptr, factory);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  ASSERT_NE(nullptr, message);

  envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
  file_access_log.set_path("/dev/null");
  file_access_log.set_format("%START_TIME%");
  TestUtility::jsonConvert(file_access_log, *message);

  AccessLog::FilterPtr filter;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AccessLog::InstanceSharedPtr instance =
      factory->createAccessLogInstance(*message, std::move(filter), context);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(instance.get()));
}

TEST(FileAccessLogConfigTest, DEPRECATED_FEATURE_TEST(FileAccessLogJsonTest)) {
  envoy::config::accesslog::v3::AccessLog config;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Provided name for static registration lookup was empty.");

  config.set_name("INVALID");

  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Didn't find a registered implementation for name: 'INVALID'");

  envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
  fal_config.set_path("/dev/null");

  ProtobufWkt::Value string_value;
  string_value.set_string_value("%PROTOCOL%");

  auto json_format = fal_config.mutable_json_format();
  (*json_format->mutable_fields())["protocol"] = string_value;

  EXPECT_EQ(
      fal_config.access_log_format_case(),
      envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kJsonFormat);
  config.mutable_typed_config()->PackFrom(fal_config);

  config.set_name(AccessLogNames::get().File);

  AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(config, context);

  EXPECT_NE(nullptr, log);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));
}

TEST(FileAccessLogConfigTest, DEPRECATED_FEATURE_TEST(FileAccessLogTypedJsonTest)) {
  envoy::config::accesslog::v3::AccessLog config;

  envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
  fal_config.set_path("/dev/null");

  ProtobufWkt::Value string_value;
  string_value.set_string_value("%PROTOCOL%");

  auto json_format = fal_config.mutable_typed_json_format();
  (*json_format->mutable_fields())["protocol"] = string_value;

  EXPECT_EQ(fal_config.access_log_format_case(),
            envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
                kTypedJsonFormat);
  config.mutable_typed_config()->PackFrom(fal_config);

  config.set_name(AccessLogNames::get().File);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(config, context);

  EXPECT_NE(nullptr, log);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));
}

TEST(FileAccessLogConfigTest, DEPRECATED_FEATURE_TEST(FileAccessLogDeprecatedFormat)) {
  const std::vector<std::string> configs{
      R"(
  path: "/foo"
  format: "plain_text"
)",
      R"(
  path: "/foo"
  json_format:
    text: "plain_text"
)",
      R"(
  path: "/foo"
  typed_json_format:
    text: "plain_text"
)",
  };

  for (const auto& yaml : configs) {
    envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
    TestUtility::loadFromYaml(yaml, fal_config);

    envoy::config::accesslog::v3::AccessLog config;
    config.mutable_typed_config()->PackFrom(fal_config);

    NiceMock<Server::Configuration::MockFactoryContext> context;
    AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(config, context);

    EXPECT_NE(nullptr, log);
    EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));
  }
}

TEST(FileAccessLogConfigTest, FileAccessLogCheckLogFormat) {
  const std::vector<std::string> configs{
      // log_format: text_format
      R"(
  path: "/foo"
  log_format:
    text_format: "plain_text"
)",

      // log_format: json_format
      R"(
  path: "/foo"
  log_format:
    json_format:
      text: "plain_text"
)",
  };

  for (const auto& yaml : configs) {
    envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
    TestUtility::loadFromYaml(yaml, fal_config);

    envoy::config::accesslog::v3::AccessLog config;
    config.mutable_typed_config()->PackFrom(fal_config);

    NiceMock<Server::Configuration::MockFactoryContext> context;
    AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(config, context);

    EXPECT_NE(nullptr, log);
    EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));
  }
}

} // namespace
} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
