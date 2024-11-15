#include "envoy/network/transport_socket.h"

#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/transport_sockets/alts/noop_transport_socket_callbacks.h"

#include "test/mocks/network/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

class TestTransportSocketCallbacks : public Network::TransportSocketCallbacks {
public:
  explicit TestTransportSocketCallbacks(Network::Connection& connection)
      : io_handle_(std::make_unique<Network::IoSocketHandleImpl>()), connection_(connection) {}

  ~TestTransportSocketCallbacks() override = default;
  Network::IoHandle& ioHandle() override { return *io_handle_; }
  const Network::IoHandle& ioHandle() const override { return *io_handle_; }
  Network::Connection& connection() override { return connection_; }
  bool shouldDrainReadBuffer() override { return false; }
  void setTransportSocketIsReadable() override { transport_socket_is_readable_ = true; }
  void raiseEvent(Network::ConnectionEvent) override { event_raised_ = true; }
  void flushWriteBuffer() override { write_buffer_flushed_ = true; }

  bool eventRaised() const { return event_raised_; }
  bool transportSocketIsReadable() const { return transport_socket_is_readable_; }
  bool writeBufferFlushed() const { return write_buffer_flushed_; }

private:
  bool event_raised_{false};
  bool transport_socket_is_readable_{false};
  bool write_buffer_flushed_{false};
  Network::IoHandlePtr io_handle_;
  Network::Connection& connection_;
};

class NoOpTransportSocketCallbacksTest : public testing::Test {
protected:
  NoOpTransportSocketCallbacksTest()
      : wrapper_callbacks_(connection_), wrapped_callbacks_(wrapper_callbacks_) {}

  Network::MockConnection connection_;
  TestTransportSocketCallbacks wrapper_callbacks_;
  NoOpTransportSocketCallbacks wrapped_callbacks_;
};

TEST_F(NoOpTransportSocketCallbacksTest, TestAllCallbacks) {
  EXPECT_EQ(&wrapper_callbacks_.ioHandle(), &wrapped_callbacks_.ioHandle());
  EXPECT_EQ(&connection_, &wrapped_callbacks_.connection());
  EXPECT_FALSE(wrapped_callbacks_.shouldDrainReadBuffer());

  wrapped_callbacks_.setTransportSocketIsReadable();
  EXPECT_FALSE(wrapper_callbacks_.transportSocketIsReadable());
  wrapped_callbacks_.raiseEvent(Network::ConnectionEvent::Connected);
  EXPECT_FALSE(wrapper_callbacks_.eventRaised());
  wrapped_callbacks_.flushWriteBuffer();
  EXPECT_FALSE(wrapper_callbacks_.writeBufferFlushed());
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
