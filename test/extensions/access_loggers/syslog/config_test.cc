#include "envoy/common/exception.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"

#include "source/extensions/access_loggers/syslog/config.h"

#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/server_factory_context.h"
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

const std::string TestAccessLog = R"([2026-08-19T14:20:54.123Z] "GET / HTTP/1.1" 200 12 1ms)";

SyslogAccessLogConfig makeClusterConfig(absl::string_view name = "syslog") {
  SyslogAccessLogConfig config;
  config.mutable_cluster()->set_name(name);
  config.set_no_hostname(true);
  config.mutable_log_format()->mutable_text_format_source()->set_inline_string(TestAccessLog);
  return config;
}

SyslogAccessLogConfig makeServerConfig() {
  SyslogAccessLogConfig config;
  config.set_no_hostname(true);
  auto* socket_address = config.mutable_server()->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(514);
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  config.mutable_log_format()->mutable_text_format_source()->set_inline_string(TestAccessLog);
  return config;
}

TEST(SyslogConfigTest, RejectsTcpServerAndClusterDestinations) {
  envoy::config::core::v3::Address address;
  address.mutable_socket_address()->set_address("127.0.0.1");
  const absl::Status tcp_status = validateServerAddress(address);
  EXPECT_EQ(absl::StatusCode::kUnimplemented, tcp_status.code());
  EXPECT_EQ("syslog over TCP is not implemented yet", tcp_status.message());

  auto config = makeClusterConfig();
  config.mutable_cluster()->set_protocol(SyslogAccessLogConfig::Cluster::TCP);

  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  context.server_context_.cluster_manager_.initializeClusters({"syslog"}, {});
  EXPECT_CALL(context.server_context_.cluster_manager_, checkActiveStaticCluster("syslog"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "syslog over TCP is not implemented yet");
}

TEST(SyslogConfigTest, RejectsInvalidServerProtocolAndPorts) {
  envoy::config::core::v3::Address address;
  address.mutable_socket_address()->set_address("127.0.0.1");
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
}

TEST(SyslogConfigTest, AcceptsUnixDomainSocket) {
  envoy::config::core::v3::Address address;
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

TEST(SyslogConfigTest, RejectsNetworkNamespaceOutsideLinux) {
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

TEST(SyslogConfigTest, RejectsUnsetAndInternalAddresses) {
  envoy::config::core::v3::Address address;
  EXPECT_EQ("syslog server address must be configured", validateServerAddress(address).message());

  address.mutable_envoy_internal_address()->set_server_listener_name("listener");
  EXPECT_EQ("syslog server does not support Envoy internal addresses",
            validateServerAddress(address).message());
}

TEST(SyslogConfigTest, FactoryRejectsUnknownCluster) {
  const auto config = makeClusterConfig("unknown");
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_CALL(context.server_context_.cluster_manager_, checkActiveStaticCluster("unknown"))
      .WillOnce(Return(absl::InvalidArgumentError("unknown cluster")));

  EXPECT_THROW_WITH_MESSAGE(
      SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context), EnvoyException,
      "syslog cluster 'unknown' must refer to an active static cluster: unknown cluster");
}

TEST(SyslogConfigTest, FactoryCreatesUdpAccessLogger) {
  const auto config = makeServerConfig();
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;

  EXPECT_NE(nullptr, SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context));
}

TEST(SyslogConfigTest, FactoryCreatesClusterAccessLogger) {
  const auto config = makeClusterConfig();
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  context.server_context_.cluster_manager_.initializeClusters({"syslog"}, {});
  EXPECT_CALL(context.server_context_.cluster_manager_, checkActiveStaticCluster("syslog"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_NE(nullptr, SyslogAccessLogFactory().createAccessLogInstance(config, nullptr, context));
}

TEST(SyslogConfigTest, RejectsSecureClusterTransport) {
  const auto config = makeClusterConfig();
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
