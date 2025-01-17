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
        THROW_OR_RETURN_VALUE(Network::Utility::resolveUrl(input.sock().local_address()),
                              Network::Address::InstanceConstSharedPtr));
  } catch (const EnvoyException& e) {
    socket_.connectionInfoProvider().setLocalAddress(THROW_OR_RETURN_VALUE(
        Network::Utility::resolveUrl("tcp://0.0.0.0:0"), Network::Address::InstanceConstSharedPtr));
  }
  try {
    socket_.connectionInfoProvider().setRemoteAddress(
        THROW_OR_RETURN_VALUE(Network::Utility::resolveUrl(input.sock().remote_address()),
                              Network::Address::InstanceConstSharedPtr));
  } catch (const EnvoyException& e) {
    socket_.connectionInfoProvider().setRemoteAddress(THROW_OR_RETURN_VALUE(
        Network::Utility::resolveUrl("tcp://0.0.0.0:0"), Network::Address::InstanceConstSharedPtr));
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
      init_manager_(nullptr),
      listener_info_(std::make_shared<NiceMock<Network::MockListenerInfo>>()) {
  socket_factories_.emplace_back(std::make_unique<Network::MockListenSocketFactory>());
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(socket_factories_[0].get()),
              socketType())
      .WillOnce(Return(Network::Socket::Type::Stream));
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(socket_factories_[0].get()),
              localAddress())
      .WillRepeatedly(ReturnRef(socket_->connectionInfoProvider().localAddress()));
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(socket_factories_[0].get()),
              getListenSocket(_))
      .WillOnce(Return(socket_));
  connection_handler_->addListener(absl::nullopt, *this, runtime_, random_);
  conn_ = dispatcher_->createClientConnection(
      socket_->connectionInfoProvider().localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr, nullptr);
  conn_->addConnectionCallbacks(connection_callbacks_);
}

void ListenerFilterWithDataFuzzer::connect() {
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillOnce(Invoke([this](Network::ListenerFilterManager& filter_manager) -> bool {
        ASSERT(filter_ != nullptr);
        filter_manager.addAcceptFilter(nullptr, std::move(filter_));
        dispatcher_->exit();
        return true;
      }));
  conn_->connect();

  EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        connection_established_ = true;
        dispatcher_->exit();
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

void ListenerFilterWithDataFuzzer::disconnect() {
  if (connection_established_) {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    conn_->close(Network::ConnectionCloseType::NoFlush);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }
}

void ListenerFilterWithDataFuzzer::fuzz(
    Network::ListenerFilterPtr filter,
    const test::extensions::filters::listener::FilterFuzzWithDataTestCase& input) {
  filter_ = std::move(filter);
  connect();
  for (int i = 0; i < input.data_size(); i++) {
    std::string data(input.data(i).begin(), input.data(i).end());
    write(data);
  }
  disconnect();
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
