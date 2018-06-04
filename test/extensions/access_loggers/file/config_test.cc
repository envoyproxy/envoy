#include "envoy/config/accesslog/v2/file.pb.h"
#include "envoy/registry/registry.h"

#include "common/access_log/access_log_impl.h"

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

  config.set_name(AccessLogNames::get().FILE);

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
          AccessLogNames::get().FILE);
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

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
