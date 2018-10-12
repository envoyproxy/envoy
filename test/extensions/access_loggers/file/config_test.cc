#include "envoy/config/accesslog/v2/file.pb.h"
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

TEST(FileAccessLogConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(FileAccessLogFactory().createAccessLogInstance(
                   envoy::config::accesslog::v2::FileAccessLog(), nullptr, context),
               ProtoValidationException);
}

TEST(FileAccessLogConfigTest, ConfigureFromProto) {
  envoy::config::filter::accesslog::v2::AccessLog config;

  envoy::config::accesslog::v2::FileAccessLog fal_config;
  fal_config.set_path("/dev/null");

  MessageUtil::jsonConvert(fal_config, *config.mutable_config());

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Provided name for static registration lookup was empty.");

  config.set_name(AccessLogNames::get().File);

  AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(config, context);

  EXPECT_NE(nullptr, log);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));

  config.set_name("INVALID");

  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Didn't find a registered implementation for name: 'INVALID'");
}

TEST(FileAccessLogConfigTest, FileAccessLogTest) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::AccessLogInstanceFactory>::getFactory(
          AccessLogNames::get().File);
  ASSERT_NE(nullptr, factory);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  ASSERT_NE(nullptr, message);

  envoy::config::accesslog::v2::FileAccessLog file_access_log;
  file_access_log.set_path("/dev/null");
  file_access_log.set_format("%START_TIME%");
  MessageUtil::jsonConvert(file_access_log, *message);

  AccessLog::FilterPtr filter;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AccessLog::InstanceSharedPtr instance =
      factory->createAccessLogInstance(*message, std::move(filter), context);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(instance.get()));
}

TEST(FileAccessLogConfigTest, FileAccessLogJsonTest) {
  envoy::config::filter::accesslog::v2::AccessLog config;

  envoy::config::accesslog::v2::FileAccessLog fal_config;
  fal_config.set_path("/dev/null");

  auto json_format = new ProtobufWkt::Struct;
  ProtobufWkt::Value string_value;
  string_value.set_string_value("%PROTOCOL%");
  (*json_format->mutable_fields())[std::string{"protocol"}] = string_value;
  fal_config.set_allocated_json_format(json_format);

  EXPECT_EQ(fal_config.access_log_format_case(),
            envoy::config::accesslog::v2::FileAccessLog::kJsonFormat);
  MessageUtil::jsonConvert(fal_config, *config.mutable_config());

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Provided name for static registration lookup was empty.");

  config.set_name(AccessLogNames::get().File);

  AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(config, context);

  EXPECT_NE(nullptr, log);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));

  config.set_name("INVALID");

  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Didn't find a registered implementation for name: 'INVALID'");
}

TEST(FileAccessLogConfigTest, FileAccessLogJsonConversionTest) {
  {
    // Make sure we fail if you set a bool value in the format dictionary
    envoy::config::filter::accesslog::v2::AccessLog config;
    config.set_name(AccessLogNames::get().File);
    envoy::config::accesslog::v2::FileAccessLog fal_config;
    fal_config.set_path("/dev/null");

    auto json_format = new ProtobufWkt::Struct;
    ProtobufWkt::Value string_value;
    string_value.set_bool_value(false);
    (*json_format->mutable_fields())[std::string{"protocol"}] = string_value;
    fal_config.set_allocated_json_format(json_format);

    MessageUtil::jsonConvert(fal_config, *config.mutable_config());
    NiceMock<Server::Configuration::MockFactoryContext> context;

    EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context),
                              EnvoyException,
                              "Only string values are supported in the JSON access log format.");
  }

  {
    // Make sure we fail if you set a nested Struct value in the format dictionary
    envoy::config::filter::accesslog::v2::AccessLog config;
    config.set_name(AccessLogNames::get().File);
    envoy::config::accesslog::v2::FileAccessLog fal_config;
    fal_config.set_path("/dev/null");

    auto json_format = new ProtobufWkt::Struct;
    auto nested_struct = new ProtobufWkt::Struct;
    ProtobufWkt::Value string_value;
    string_value.set_string_value(std::string{"some_nested_value"});
    (*nested_struct->mutable_fields())[std::string{"some_nested_key"}] = string_value;

    ProtobufWkt::Value struct_value;
    struct_value.set_allocated_struct_value(nested_struct);
    (*json_format->mutable_fields())[std::string{"top_level_key"}] = struct_value;
    fal_config.set_allocated_json_format(json_format);

    MessageUtil::jsonConvert(fal_config, *config.mutable_config());
    NiceMock<Server::Configuration::MockFactoryContext> context;

    EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context),
                              EnvoyException,
                              "Only string values are supported in the JSON access log format.");
  }
}

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
