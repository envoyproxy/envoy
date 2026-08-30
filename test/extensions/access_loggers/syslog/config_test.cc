#include <cstring>

#include "envoy/access_log/access_log_config.h"
#include "envoy/common/exception.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/access_loggers/syslog/config.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/transport_socket.h"
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

constexpr absl::string_view UdpServerConfigYaml = R"EOF(
server:
  socket_address:
    address: 127.0.0.1
    port_value: 514
    protocol: UDP
no_hostname: true
stat_prefix: test
log_format:
  text_format_source:
    inline_string: test
)EOF";

constexpr absl::string_view UdpClusterConfigYaml = R"EOF(
cluster:
  name: syslog
  protocol: UDP
no_hostname: true
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

TEST_F(SyslogConfigTest, RejectsInvalidServerAddresses) {
  envoy::config::core::v3::Address address;
  EXPECT_EQ("syslog server address must be configured", validateServerAddress(address).message());

  address.mutable_envoy_internal_address()->set_server_listener_name("listener");
  EXPECT_EQ("syslog server does not support Envoy internal addresses",
            validateServerAddress(address).message());

  address.mutable_socket_address()->set_address("127.0.0.1");
  EXPECT_EQ("syslog over TCP is not implemented yet", validateServerAddress(address).message());
  EXPECT_EQ(absl::StatusCode::kUnimplemented, validateServerAddress(address).code());

  address.mutable_socket_address()->set_protocol(
      static_cast<envoy::config::core::v3::SocketAddress::Protocol>(100));
  EXPECT_EQ("invalid syslog server protocol value: 100", validateServerAddress(address).message());

  address.mutable_socket_address()->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  EXPECT_EQ("syslog server port must be configured", validateServerAddress(address).message());

  address.mutable_socket_address()->set_port_value(0);
  EXPECT_EQ("syslog server port must not be zero", validateServerAddress(address).message());

  address.mutable_socket_address()->set_named_port("syslog");
  EXPECT_EQ("syslog server port must be a numeric port value",
            validateServerAddress(address).message());

  address.mutable_pipe()->set_path("/tmp/syslog.sock");
#ifdef WIN32
  EXPECT_EQ("syslog Unix domain sockets are not supported on Windows",
            validateServerAddress(address).message());
#else
  EXPECT_TRUE(validateServerAddress(address).ok());
  address.mutable_pipe()->clear_path();
  EXPECT_EQ("syslog Unix domain socket path must not be empty",
            validateServerAddress(address).message());
#endif
}

TEST_F(SyslogConfigTest, RejectsNetworkNamespaceOutsideLinux) {
  envoy::config::core::v3::Address address;
  auto* socket_address = address.mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(514);
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_network_namespace_filepath("/var/run/netns/syslog");
#ifdef __linux__
  EXPECT_TRUE(validateServerAddress(address).ok());
#else
  EXPECT_EQ("syslog network namespace is only supported on Linux",
            validateServerAddress(address).message());
#endif
}

TEST_F(SyslogConfigTest, RejectsEmptyStatPrefix) {
  auto config = loadConfig(UdpServerConfigYaml);
  config.clear_stat_prefix();
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, RejectsInvalidFieldConstraints) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto config = loadConfig(UdpServerConfigYaml);
  config.set_max_syslog_msg_bytes(479);
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);

  config = loadConfig(UdpServerConfigYaml);
  config.set_max_syslog_msg_bytes(65508);
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);

  config = loadConfig(UdpServerConfigYaml);
  config.set_tag(std::string(33, 'a'));
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);

  config = loadConfig(UdpServerConfigYaml);
  config.set_msg_id("has space");
  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, ValidatesHostnameRequirement) {
  auto config = loadConfig(UdpServerConfigYaml);
  config.set_no_hostname(false);
  testing::NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  expectGethostname(os_sys_calls, "testhost", 0);
  EXPECT_TRUE(validateSyslogConfig(config).ok());

  expectGethostname(os_sys_calls, "", -1);
  EXPECT_EQ("syslog local hostname is unavailable; set no_hostname to true to omit it",
            validateSyslogConfig(config).message());

  expectGethostname(os_sys_calls, "", 0);
  EXPECT_EQ("syslog local hostname is unavailable; set no_hostname to true to omit it",
            validateSyslogConfig(config).message());
}

TEST_F(SyslogConfigTest, RejectsTcpDestinations) {
  auto server_config = loadConfig(UdpServerConfigYaml);
  server_config.mutable_server()->mutable_socket_address()->set_protocol(
      envoy::config::core::v3::SocketAddress::TCP);
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> server_context;

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(server_config, nullptr, server_context),
      EnvoyException, "syslog over TCP is not implemented yet");

  auto cluster_config = loadConfig(UdpClusterConfigYaml);
  cluster_config.mutable_cluster()->set_protocol(SyslogAccessLogConfig::Cluster::TCP);
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> cluster_context;
  cluster_context.server_context_.cluster_manager_.initializeClusters({"syslog"}, {});
  EXPECT_CALL(cluster_context.server_context_.cluster_manager_, checkActiveStaticCluster("syslog"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(cluster_config, nullptr, cluster_context),
      EnvoyException, "syslog over TCP is not implemented yet");
}

TEST_F(SyslogConfigTest, FactoryRejectsUnknownCluster) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.mutable_cluster()->set_name("unknown");
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

TEST_F(SyslogConfigTest, FactoryRejectsInvalidClusterProtocol) {
  auto config = loadConfig(UdpClusterConfigYaml);
  config.mutable_cluster()->set_protocol(static_cast<SyslogAccessLogConfig::Cluster::Protocol>(99));
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_THROW(SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context),
               ProtoValidationException);
}

TEST_F(SyslogConfigTest, RejectsSecureClusterTransport) {
  const auto config = loadConfig(UdpClusterConfigYaml);
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  context.server_context_.cluster_manager_.initializeClusters({"syslog"}, {});
  EXPECT_CALL(context.server_context_.cluster_manager_, checkActiveStaticCluster("syslog"))
      .WillOnce(Return(absl::OkStatus()));
  auto& cluster = *context.server_context_.cluster_manager_.active_clusters_.at("syslog");
  auto& matcher =
      dynamic_cast<Upstream::MockTransportSocketMatcher&>(cluster.info_->transportSocketMatcher());
  auto secure_factory = std::make_unique<testing::NiceMock<Network::MockTransportSocketFactory>>();
  ON_CALL(*secure_factory, implementsSecureTransport()).WillByDefault(Return(true));
  auto* secure_factory_ptr = secure_factory.get();
  matcher.socket_factory_ = std::move(secure_factory);
  EXPECT_CALL(matcher, resolve(nullptr, nullptr, testing::_))
      .WillOnce(Return(
          Upstream::TransportSocketMatcher::MatchData(*secure_factory_ptr, matcher.stats_, "tls")));

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "syslog over TLS is not implemented yet");
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
