#include <cstring>

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

constexpr absl::string_view PipeConfigYaml = R"EOF(
pipe:
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

void expectGethostname(Api::MockOsSysCalls& os_sys_calls, absl::string_view hostname, int rc) {
  EXPECT_CALL(os_sys_calls, gethostname(testing::_, testing::_))
      .WillOnce([hostname, rc](char* name, size_t length) -> Api::SysCallIntResult {
        if (rc == 0 && length > 0) {
          const size_t n = std::min(hostname.size(), length - 1);
          memcpy(name, hostname.data(), n);
          name[n] = '\0';
        }
        return {rc, rc == 0 ? 0 : 1};
      });
}

class SyslogConfigTest : public testing::Test {};

TEST_F(SyslogConfigTest, FactoryRegistersNameAndEmptyConfig) {
  SyslogAccessLogFactory factory;

  EXPECT_EQ("envoy.access_loggers.syslog", factory.name());
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_NE(nullptr, Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
                         "envoy.access_loggers.syslog"));
}

TEST_F(SyslogConfigTest, ValidatesPipe) {
  auto config = loadConfig(PipeConfigYaml);
#ifdef WIN32
  EXPECT_EQ("syslog Unix domain sockets are not supported on Windows",
            validateSyslogConfig(config).message());
#else
  EXPECT_TRUE(validateSyslogConfig(config).ok());
  config.mutable_pipe()->clear_path();
  EXPECT_EQ("syslog Unix domain socket path must not be empty",
            validateSyslogConfig(config).message());
#endif
}

TEST_F(SyslogConfigTest, RejectsEmptyStatPrefix) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.clear_stat_prefix();
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, RejectsMissingDestination) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.clear_cluster_name();
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, RejectsEmptyCluster) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.set_cluster_name("");
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, RejectsInvalidFieldConstraints) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto config = loadConfig(UdpClusterConfigYaml);
  config.set_max_syslog_msg_bytes(479);
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);

  config = loadConfig(UdpClusterConfigYaml);
  config.set_max_syslog_msg_bytes(65508);
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);

  config = loadConfig(UdpClusterConfigYaml);
  config.set_tag(std::string(33, 'a'));
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);

  config = loadConfig(UdpClusterConfigYaml);
  config.set_msg_id("has space");
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, ValidatesHostnameRequirement) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.set_omit_hostname(false);
  testing::NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  expectGethostname(os_sys_calls, "testhost", 0);
  EXPECT_TRUE(validateSyslogConfig(config).ok());

  expectGethostname(os_sys_calls, "", -1);
  EXPECT_EQ("syslog local hostname is unavailable; set omit_hostname to true to omit it",
            validateSyslogConfig(config).message());

  expectGethostname(os_sys_calls, "", 0);
  EXPECT_EQ("syslog local hostname is unavailable; set omit_hostname to true to omit it",
            validateSyslogConfig(config).message());
}

TEST_F(SyslogConfigTest, FactoryRejectsUnknownCluster) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.set_cluster_name("unknown");
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_CALL(context.server_context_.cluster_manager_, checkActiveStaticCluster("unknown"))
      .WillOnce(Return(absl::InvalidArgumentError("unknown cluster")));

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "syslog cluster 'unknown' must refer to an active static cluster: unknown cluster");
}

TEST_F(SyslogConfigTest, FactoryRejectsInactiveCluster) {
  auto config = loadConfig(UdpClusterConfigYaml);
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_CALL(context.server_context_.cluster_manager_, checkActiveStaticCluster("syslog"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "cluster 'syslog' is not active");
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
