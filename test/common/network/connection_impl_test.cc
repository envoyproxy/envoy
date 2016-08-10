#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/connection_impl.h"

#include "test/mocks/network/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Return;
using testing::Test;

namespace Network {

class ConnectionImplForTest : public ConnectionImpl {
public:
  using ConnectionImpl::ConnectionImpl;

  void raiseDisconnect() { onEvent(0x20); }
};

TEST(ConnectionImplTest, BufferCallbacks) {
  InSequence s;

  Event::DispatcherImpl dispatcher;
  ConnectionImplForTest connection(dispatcher);
  MockConnectionCallbacks callbacks;
  connection.addConnectionCallbacks(callbacks);

  EXPECT_EQ("", connection.nextProtocol());

  std::shared_ptr<MockWriteFilter> write_filter(new MockWriteFilter());
  std::shared_ptr<MockFilter> filter(new MockFilter());
  connection.addWriteFilter(write_filter);
  connection.addFilter(filter);

  EXPECT_CALL(*write_filter, onWrite(_)).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*write_filter, onWrite(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onWrite(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(callbacks, onBufferChange(ConnectionBufferType::Write, 0, 4));
  EXPECT_CALL(callbacks, onBufferChange(ConnectionBufferType::Write, 4, -4));
  EXPECT_CALL(callbacks, onEvent(ConnectionEvent::RemoteClose));

  Buffer::OwnedImpl data("1234");
  connection.write(data);
  connection.write(data);
  connection.raiseDisconnect();
}

TEST(TcpClientConnectionImplTest, BadConnectNotConnRefused) {
  Event::DispatcherImpl dispatcher;
  // Connecting to 255.255.255.255 will cause a perm error and not ECONNREFUSED which is a
  // different path in libevent. Make sure this doesn't crash.
  ClientConnectionPtr connection = dispatcher.createClientConnection("tcp://255.255.255.255:1");
  connection->connect();
  connection->noDelay(true);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(TcpClientConnectionImplTest, BadConnectConnRefused) {
  Event::DispatcherImpl dispatcher;
  // Connecting to an invalid port on localhost will cause ECONNREFUSED which is a different code
  // path from other errors. Test this also.
  ClientConnectionPtr connection = dispatcher.createClientConnection("tcp://255.255.255.255:1");
  connection->connect();
  connection->noDelay(true);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // Network
