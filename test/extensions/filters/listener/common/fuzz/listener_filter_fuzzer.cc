#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

void ListenerFilterFuzzer::fuzz(
    Network::ListenerFilterPtr filter,
    const test::extensions::filters::listener::FilterFuzzTestCase& input) {
  try {
    socket_.connectionInfoProvider().setLocalAddress(
        Network::Utility::resolveUrl(input.sock().local_address()));
  } catch (const EnvoyException& e) {
    socket_.connectionInfoProvider().setLocalAddress(
        Network::Utility::resolveUrl("tcp://0.0.0.0:0"));
  }
  try {
    socket_.connectionInfoProvider().setRemoteAddress(
        Network::Utility::resolveUrl(input.sock().remote_address()));
  } catch (const EnvoyException& e) {
    socket_.connectionInfoProvider().setRemoteAddress(
        Network::Utility::resolveUrl("tcp://0.0.0.0:0"));
  }

  filter->onAccept(cb_);
}

ListenerFilterWithDataFuzzer::ListenerFilterWithDataFuzzer()
    : api_(Api::createApiForTest(stats_store_)),
      dispatcher_(api_->allocateDispatcher("test_thread")),
      socket_(std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
          Network::Test::getCanonicalLoopbackAddress(Network::Address::IpVersion::v4))),
      connection_handler_(new Server::ConnectionHandlerImpl(*dispatcher_, absl::nullopt)),
      name_("proxy"), filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()),
      init_manager_(nullptr) {
  EXPECT_CALL(socket_factory_, socketType()).WillOnce(Return(Network::Socket::Type::Stream));
  EXPECT_CALL(socket_factory_, localAddress())
      .WillRepeatedly(ReturnRef(socket_->connectionInfoProvider().localAddress()));
  EXPECT_CALL(socket_factory_, getListenSocket(_)).WillOnce(Return(socket_));
  connection_handler_->addListener(absl::nullopt, *this, runtime_);
  conn_ = dispatcher_->createClientConnection(socket_->connectionInfoProvider().localAddress(),
                                              Network::Address::InstanceConstSharedPtr(),
                                              Network::Test::createRawBufferSocket(), nullptr);
  conn_->addConnectionCallbacks(connection_callbacks_);
}

void ListenerFilterWithDataFuzzer::connect(Network::ListenerFilterPtr filter) {
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillOnce(Invoke([&](Network::ListenerFilterManager& filter_manager) -> bool {
        filter_manager.addAcceptFilter(nullptr, std::move(filter));
        dispatcher_->exit();
        return true;
      }));
  conn_->connect();

  EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

void ListenerFilterWithDataFuzzer::disconnect() {
  EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  conn_->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

void ListenerFilterWithDataFuzzer::fuzz(
    Network::ListenerFilterPtr filter,
    const test::extensions::filters::listener::FilterFuzzWithDataTestCase& input) {
  connect(std::move(filter));
  for (int i = 0; i < input.data_size(); i++) {
    std::string data(input.data(i).begin(), input.data(i).end());
    write(data);
  }
  disconnect();
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
