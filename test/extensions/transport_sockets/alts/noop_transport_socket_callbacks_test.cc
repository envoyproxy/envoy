#include "extensions/transport_sockets/alts/noop_transport_socket_callbacks.h"

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
      : connection_(connection) {}

  int fd() const override { return 1; }
  Network::Connection& connection() override { return connection_; }
  bool shouldDrainReadBuffer() override { return false; }
  void setReadBufferReady() override { set_read_buffer_ready_ = true; }
  void raiseEvent(Network::ConnectionEvent) override { event_raised_ = true; }

  bool event_raised() const { return event_raised_; }
  bool set_read_buffer_ready() const { return set_read_buffer_ready_; }

private:
  bool event_raised_{false};
  bool set_read_buffer_ready_{false};
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
  EXPECT_EQ(wrapper_callbacks_.fd(), wrapped_callbacks_.fd());
  EXPECT_EQ(&connection_, &wrapped_callbacks_.connection());
  EXPECT_FALSE(wrapped_callbacks_.shouldDrainReadBuffer());

  wrapped_callbacks_.setReadBufferReady();
  EXPECT_FALSE(wrapper_callbacks_.set_read_buffer_ready());
  wrapped_callbacks_.raiseEvent(Network::ConnectionEvent::Connected);
  EXPECT_FALSE(wrapper_callbacks_.event_raised());
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
