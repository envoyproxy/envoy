#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/network/socket_interface.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

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

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
