#include "envoy/buffer/buffer.h"
#include "envoy/event/file_event.h"
#include "envoy/network/listen_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fine_grain_logger.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/bootstrap/internal_listener/client_connection_factory.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/network_utility.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

namespace {

class MockInternalListenerManager : public Network::InternalListenerManager {
public:
  MOCK_METHOD(Network::InternalListenerOptRef, findByAddress,
              (const Network::Address::InstanceConstSharedPtr&));
};

// The internal connection factory is linked in this test suite. This test suite verifies the
// connection can be created.
class ClientConnectionFactoryTest : public testing::Test {
public:
  ClientConnectionFactoryTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        buf_(1024) {
    std::tie(io_handle_, io_handle_peer_) =
        IoSocket::UserSpace::IoHandleFactory::createIoHandlePair();
    EXPECT_CALL(tls_allocator_, allocateSlot());
  }
  void setupTlsSlot() {
    tls_slot_ =
        ThreadLocal::TypedSlot<Bootstrap::InternalListener::ThreadLocalRegistryImpl>::makeUnique(
            tls_allocator_);
    tls_slot_->set([r = registry_](Event::Dispatcher&) { return r; });
    // TODO: restore the original value via RAII.
    Bootstrap::InternalListener::InternalClientConnectionFactory::registry_tls_slot_ =
        tls_slot_.get();
  }
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  MockInternalListenerManager internal_listener_manager_;
  std::shared_ptr<ThreadLocalRegistryImpl> registry_{std::make_shared<ThreadLocalRegistryImpl>()};
  ThreadLocal::MockInstance tls_allocator_;
  std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalRegistryImpl>> tls_slot_;

  // Owned by IoHandleImpl.
  NiceMock<Event::MockSchedulableCallback>* schedulable_cb_;
  std::unique_ptr<IoSocket::UserSpace::IoHandleImpl> io_handle_;
  std::unique_ptr<IoSocket::UserSpace::IoHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
  Network::Address::EnvoyInternalInstance listener_addr{"listener_internal_address"};
};

class MockInternalListener : public Network::InternalListener {
public:
  MOCK_METHOD(uint64_t, listenerTag, ());
  MOCK_METHOD(Network::Listener*, listener, ());
  MOCK_METHOD(void, pauseListening, ());
  MOCK_METHOD(void, resumeListening, ());
  MOCK_METHOD(void, shutdownListener, ());
  MOCK_METHOD(void, updateListenerConfig, (Network::ListenerConfig&));
  MOCK_METHOD(void, onFilterChainDraining, (const std::list<const Network::FilterChain*>&));
  MOCK_METHOD(void, onAccept, (Network::ConnectionSocketPtr &&));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

TEST_F(ClientConnectionFactoryTest,
       ConnectFailsIfInternalConnectionThreadLocalRegistryIsNotPublished) {
  tls_slot_ =
      ThreadLocal::TypedSlot<Bootstrap::InternalListener::ThreadLocalRegistryImpl>::makeUnique(
          tls_allocator_);
  // This slot set publish a nullptr.
  tls_slot_->set([](Event::Dispatcher&) { return nullptr; });
  Bootstrap::InternalListener::InternalClientConnectionFactory::registry_tls_slot_ =
      tls_slot_.get();
  auto client_conn = dispatcher_->createClientConnection(
      std::make_shared<Network::Address::EnvoyInternalInstance>(listener_addr),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr,
      nullptr);
  EXPECT_NE(nullptr, client_conn);
  EXPECT_TRUE(client_conn->connecting());
  client_conn->connect();
  // Connect returns error immediately because TLS internal listener registry is not ready.
  EXPECT_FALSE(client_conn->connecting());
  client_conn->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(ClientConnectionFactoryTest, ConnectFailsIfInternalConnectionManagerNotExist) {
  setupTlsSlot();
  auto client_conn = dispatcher_->createClientConnection(
      std::make_shared<Network::Address::EnvoyInternalInstance>(listener_addr),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr,
      nullptr);
  EXPECT_NE(nullptr, client_conn);
  EXPECT_TRUE(client_conn->connecting());
  client_conn->connect();
  // Connect returns error immediately because no internal listener manager is registered.
  EXPECT_FALSE(client_conn->connecting());
  client_conn->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(ClientConnectionFactoryTest, ConnectFailsIfInternalListenerNotExist) {
  setupTlsSlot();
  registry_->setInternalListenerManager(internal_listener_manager_);

  EXPECT_CALL(internal_listener_manager_, findByAddress(_))
      .WillOnce(testing::Return(Network::InternalListenerOptRef()));

  auto client_conn = dispatcher_->createClientConnection(
      std::make_shared<Network::Address::EnvoyInternalInstance>(listener_addr),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr,
      nullptr);

  EXPECT_NE(nullptr, client_conn);
  EXPECT_TRUE(client_conn->connecting());
  client_conn->connect();
  // Connect returns error immediately because no internal listener is ready.
  EXPECT_FALSE(client_conn->connecting());
  client_conn->close(Network::ConnectionCloseType::NoFlush);
}

// Verify that the client connection to envoy internal address can be established. This test case
// does not instantiate a server connection. The server connection is tested in internal listener.
TEST_F(ClientConnectionFactoryTest, ConnectSucceeds) {
  setupTlsSlot();
  registry_->setInternalListenerManager(internal_listener_manager_);
  MockInternalListener internal_listener;
  Network::InternalListenerOptRef internal_listener_opt{internal_listener};

  EXPECT_CALL(internal_listener_manager_, findByAddress(_))
      .WillOnce(testing::Return(internal_listener_opt));
  Network::ConnectionSocketPtr server_socket;
  EXPECT_CALL(internal_listener, onAccept(_)).WillOnce([&](auto&& socket) {
    server_socket = std::move(socket);
  });

  auto client_conn = dispatcher_->createClientConnection(
      std::make_shared<Network::Address::EnvoyInternalInstance>(listener_addr),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr,
      nullptr);

  EXPECT_NE(nullptr, server_socket);

  EXPECT_NE(nullptr, client_conn);
  EXPECT_TRUE(client_conn->connecting());
  client_conn->connect();

  // Connect is successful but the connecting  state takes another poll cycle to clear.
  EXPECT_TRUE(client_conn->connecting());

  Buffer::OwnedImpl buf_to_write("0123456789");

  client_conn->write(buf_to_write, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // The write callback detects that connecting is completed.
  EXPECT_FALSE(client_conn->connecting());

  auto result = server_socket->ioHandle().recv(buf_.data(), buf_.size(), 0);
  ASSERT_EQ(10, result.return_value_);
  ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.return_value_));

  client_conn->close(Network::ConnectionCloseType::NoFlush);
  server_socket->close();
}
} // namespace
} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
