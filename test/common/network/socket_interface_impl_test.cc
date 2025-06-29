#include "envoy/extensions/network/socket_interface/v3/default_socket_interface.pb.h"

#include "source/common/network/socket_interface_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class SocketInterfaceImplTest : public ::testing::Test {
public:
  SocketInterfaceImplTest() : socket_interface_() {}

protected:
  SocketInterfaceImpl socket_interface_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(SocketInterfaceImplTest, CreateEmptyConfigProto) {
  auto config = socket_interface_.createEmptyConfigProto();
  EXPECT_NE(config, nullptr);
  EXPECT_EQ(config->GetTypeName(),
            "envoy.extensions.network.socket_interface.v3.DefaultSocketInterface");
}

TEST_F(SocketInterfaceImplTest, ModeConversionReadWritev) {
  // Test conversion of proto READ_WRITEV to C++ IoUringMode::ReadWritev
  using ProtoConfig = envoy::extensions::network::socket_interface::v3::DefaultSocketInterface;

  ProtoConfig config;
  auto* io_uring_options = config.mutable_io_uring_options();
  io_uring_options->set_mode(envoy::extensions::network::socket_interface::v3::READ_WRITEV);

  // This indirectly tests the conversion in createBootstrapExtension
  // We can't directly test the conversion function as it's embedded in createBootstrapExtension
  EXPECT_NO_THROW(socket_interface_.createBootstrapExtension(config, context_));
}

TEST_F(SocketInterfaceImplTest, ModeConversionSendRecv) {
  // Test conversion of proto SEND_RECV to C++ IoUringMode::SendRecv
  using ProtoConfig = envoy::extensions::network::socket_interface::v3::DefaultSocketInterface;

  ProtoConfig config;
  auto* io_uring_options = config.mutable_io_uring_options();
  io_uring_options->set_mode(envoy::extensions::network::socket_interface::v3::SEND_RECV);

  EXPECT_NO_THROW(socket_interface_.createBootstrapExtension(config, context_));
}

TEST_F(SocketInterfaceImplTest, ModeConversionSendmsgRecvmsg) {
  // Test conversion of proto SENDMSG_RECVMSG to C++ IoUringMode::SendmsgRecvmsg
  using ProtoConfig = envoy::extensions::network::socket_interface::v3::DefaultSocketInterface;

  ProtoConfig config;
  auto* io_uring_options = config.mutable_io_uring_options();
  io_uring_options->set_mode(envoy::extensions::network::socket_interface::v3::SENDMSG_RECVMSG);

  EXPECT_NO_THROW(socket_interface_.createBootstrapExtension(config, context_));
}

TEST_F(SocketInterfaceImplTest, DefaultModeNoIoUringOptions) {
  // Test that not specifying io_uring_options defaults properly
  using ProtoConfig = envoy::extensions::network::socket_interface::v3::DefaultSocketInterface;

  ProtoConfig config;
  // Not setting io_uring_options at all

  EXPECT_NO_THROW(socket_interface_.createBootstrapExtension(config, context_));
}

TEST_F(SocketInterfaceImplTest, DefaultModeNoModeSpecified) {
  // Test that not specifying mode field defaults properly
  using ProtoConfig = envoy::extensions::network::socket_interface::v3::DefaultSocketInterface;

  ProtoConfig config;
  auto* io_uring_options = config.mutable_io_uring_options();
  // Not setting mode field, should default to READ_WRITEV
  io_uring_options->mutable_io_uring_size()->set_value(1000);

  EXPECT_NO_THROW(socket_interface_.createBootstrapExtension(config, context_));
}

#if defined(__linux__) && !defined(__ANDROID_API__) && defined(ENVOY_ENABLE_IO_URING)
TEST_F(SocketInterfaceImplTest, IoUringWithMode) {
  // Test that io_uring configuration with mode works on Linux
  using ProtoConfig = envoy::extensions::network::socket_interface::v3::DefaultSocketInterface;

  ProtoConfig config;
  auto* io_uring_options = config.mutable_io_uring_options();
  io_uring_options->set_mode(envoy::extensions::network::socket_interface::v3::SEND_RECV);
  io_uring_options->mutable_io_uring_size()->set_value(2000);
  io_uring_options->mutable_read_buffer_size()->set_value(16384);
  io_uring_options->set_use_submission_queue_polling(false);
  io_uring_options->mutable_write_timeout_ms()->set_value(1000);

  auto extension = socket_interface_.createBootstrapExtension(config, context_);
  EXPECT_NE(extension, nullptr);
}
#endif

} // namespace
} // namespace Network
} // namespace Envoy
