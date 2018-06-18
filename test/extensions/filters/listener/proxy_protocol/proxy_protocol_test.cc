#include <functional>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "server/connection_handler_impl.h"

#include "extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

// Build again on the basis of the connection_handler_test.cc

class ProxyProtocolTest : public testing::TestWithParam<Network::Address::IpVersion>,
                          public Network::ListenerConfig,
                          public Network::FilterChainManager,
                          protected Logger::Loggable<Logger::Id::main> {
public:
  ProxyProtocolTest()
      : socket_(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr, true),
        connection_handler_(new Server::ConnectionHandlerImpl(ENVOY_LOGGER(), dispatcher_)),
        name_("proxy"), filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()) {
    connection_handler_->addListener(*this);
    conn_ = dispatcher_.createClientConnection(socket_.localAddress(),
                                               Network::Address::InstanceConstSharedPtr(),
                                               Network::Test::createRawBufferSocket(), nullptr);
    conn_->addConnectionCallbacks(connection_callbacks_);
  }

  // Listener
  Network::FilterChainManager& filterChainManager() override { return *this; }
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::Socket& socket() override { return socket_; }
  bool bindToPort() override { return true; }
  bool handOffRestoredDestinationConnections() const override { return false; }
  uint32_t perConnectionBufferLimitBytes() override { return 0; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  const std::string& name() const override { return name_; }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  void connect(bool read = true) {
    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](Network::ListenerFilterManager& filter_manager) -> bool {
          filter_manager.addAcceptFilter(
              std::make_unique<Filter>(std::make_shared<Config>(listenerScope())));
          return true;
        }));
    conn_->connect();
    if (read) {
      read_filter_.reset(new NiceMock<Network::MockReadFilter>());
      EXPECT_CALL(factory_, createNetworkFilterChain(_, _))
          .WillOnce(Invoke([&](Network::Connection& connection,
                               const std::vector<Network::FilterFactoryCb>&) -> bool {
            server_connection_ = &connection;
            connection.addConnectionCallbacks(server_callbacks_);
            connection.addReadFilter(read_filter_);
            return true;
          }));
    }
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf, false);
  }

  void expectData(std::string expected) {
    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> Network::FilterStatus {
          EXPECT_EQ(TestUtility::bufferToString(buffer), expected);
          buffer.drain(expected.length());
          dispatcher_.exit();
          return Network::FilterStatus::Continue;
        }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    conn_->close(Network::ConnectionCloseType::NoFlush);

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void expectProxyProtoError() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);

    EXPECT_EQ(stats_store_.counter("downstream_cx_proxy_proto_error").value(), 1);
  }
