#include <string>

#include "envoy/access_log/access_log_config.h"
#include "envoy/common/exception.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/access_loggers/syslog/config.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {
namespace {

using SyslogAccessLogConfig = envoy::extensions::access_loggers::syslog::v3::SyslogAccessLogConfig;
using testing::Return;

constexpr absl::string_view UnixSocketConfigYaml = R"EOF(
unix_socket:
  path: /tmp/syslog.sock
omit_hostname: true
stat_prefix: test
log_format:
  text_format_source:
    inline_string: test
)EOF";

constexpr absl::string_view UdpClusterConfigYaml = R"EOF(
cluster_name: syslog
omit_hostname: true
stat_prefix: test
log_format:
  text_format_source:
    inline_string: test
)EOF";

SyslogAccessLogConfig loadConfig(absl::string_view yaml) {
  SyslogAccessLogConfig config;
  TestUtility::loadFromYaml(std::string(yaml), config);
  return config;
}

class SyslogConfigTest : public testing::Test {};

TEST_F(SyslogConfigTest, FactoryRegistrationAndEmptyConfig) {
  SyslogAccessLogFactory factory;

  EXPECT_EQ("envoy.access_loggers.syslog", factory.name());
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_NE(nullptr, Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
                         "envoy.access_loggers.syslog"));
}

TEST_F(SyslogConfigTest, UnixSocketValidation) {
  auto config = loadConfig(UnixSocketConfigYaml);
#ifdef WIN32
  EXPECT_EQ("syslog Unix domain sockets are not supported on Windows",
            validateSyslogConfig(config).message());
#else
  EXPECT_TRUE(validateSyslogConfig(config).ok());
  config.mutable_unix_socket()->clear_path();
  EXPECT_EQ("syslog Unix domain socket path must not be empty",
            validateSyslogConfig(config).message());
#endif
}

TEST_F(SyslogConfigTest, InvalidUnixSocketWithClusterName) {
  auto config = loadConfig(UnixSocketConfigYaml);
  config.mutable_unix_socket()->clear_path();
  config.set_cluster_name("syslog");
#ifdef WIN32
  EXPECT_EQ("syslog Unix domain sockets are not supported on Windows",
            validateSyslogConfig(config).message());
#else
  EXPECT_EQ("syslog Unix domain socket path must not be empty",
            validateSyslogConfig(config).message());
#endif
}

#ifndef WIN32
TEST_F(SyslogConfigTest, IgnoresClusterNameIfUnixSocketUsed) {
  auto config = loadConfig(UnixSocketConfigYaml);
  config.set_cluster_name("syslog");
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_CALL(context.server_context_.cluster_manager_, hasCluster(testing::_)).Times(0);

  EXPECT_NE(nullptr,
            SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context).get());
}
#endif

TEST_F(SyslogConfigTest, EmptyStatPrefix) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.clear_stat_prefix();
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, MissingUnixSocketAndClusterName) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.clear_unix_socket();
  config.clear_cluster_name();
  ASSERT_TRUE(!config.has_unix_socket() && config.cluster_name().empty());
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "at least one of 'unix_socket' or 'cluster_name' must be configured");
}

TEST_F(SyslogConfigTest, InvalidTagAndMsgId) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto config = loadConfig(UdpClusterConfigYaml);
  config.set_tag(std::string(33, 'a'));
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);

  config = loadConfig(UdpClusterConfigYaml);
  config.set_msg_id("has space");
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, HostnameValidation) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.set_omit_hostname(false);
  testing::NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  const std::string hostname = "testhost";
  EXPECT_CALL(os_sys_calls, gethostname(testing::_, testing::_))
      .WillOnce(testing::DoAll(
          testing::SetArrayArgument<0>(hostname.c_str(), hostname.c_str() + hostname.size() + 1),
          Return(Api::SysCallIntResult{0, 0})));
  EXPECT_TRUE(validateSyslogConfig(config).ok());

  EXPECT_CALL(os_sys_calls, gethostname(testing::_, testing::_))
      .WillOnce(Return(Api::SysCallIntResult{-1, 1}));
  EXPECT_EQ("syslog local hostname is unavailable; set omit_hostname to true to omit it",
            validateSyslogConfig(config).message());

  EXPECT_CALL(os_sys_calls, gethostname(testing::_, testing::_))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<0>('\0'), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ("syslog local hostname is unavailable; set omit_hostname to true to omit it",
            validateSyslogConfig(config).message());
}

TEST_F(SyslogConfigTest, NotExistingClusterName) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.set_cluster_name("unknown");
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_CALL(context.server_context_.cluster_manager_, hasCluster("unknown"))
      .WillOnce(Return(false));

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "syslog cluster 'unknown' does not exist");
}

TEST_F(SyslogConfigTest, ExistingClusterName) {
  auto config = loadConfig(UdpClusterConfigYaml);
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_CALL(context.server_context_.cluster_manager_, hasCluster("syslog"))
      .WillOnce(Return(true));

  EXPECT_NE(nullptr,
            SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context).get());
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
