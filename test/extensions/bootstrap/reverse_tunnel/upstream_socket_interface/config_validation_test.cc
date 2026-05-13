#include "envoy/extensions/access_loggers/stream/v3/stream.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"

#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ConfigValidationTest : public testing::Test {
protected:
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(ConfigValidationTest, ValidConfiguration) {
  config_.set_stat_prefix("reverse_tunnel");

  ReverseTunnelAcceptor acceptor(context_);

  EXPECT_NO_THROW(acceptor.createBootstrapExtension(config_, context_));
}

TEST_F(ConfigValidationTest, EmptyStatPrefix) {
  ReverseTunnelAcceptor acceptor(context_);

  EXPECT_NO_THROW(acceptor.createBootstrapExtension(config_, context_));
}

TEST_F(ConfigValidationTest, AccessLogConfigurationIsAccepted) {
  auto* access_log = config_.add_access_log();
  access_log->set_name("envoy.access_loggers.stdout");
  envoy::extensions::access_loggers::stream::v3::StdoutAccessLog stdout_access_log;
  stdout_access_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
      "%DYNAMIC_METADATA(envoy.reverse_tunnel.lifecycle:event)%\n");
  access_log->mutable_typed_config()->PackFrom(stdout_access_log);

  EXPECT_CALL(context_, messageValidationVisitor())
      .WillRepeatedly(testing::ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

  ReverseTunnelAcceptor acceptor(context_);
  auto extension = acceptor.createBootstrapExtension(config_, context_);
  auto* reverse_tunnel_extension = dynamic_cast<ReverseTunnelAcceptorExtension*>(extension.get());
  ASSERT_NE(reverse_tunnel_extension, nullptr);
  EXPECT_TRUE(reverse_tunnel_extension->hasAccessLogs());
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
