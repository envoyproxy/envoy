#include "extensions/transport_sockets/tap/tap.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

TapSocket::TapSocket(SocketTapConfigSharedPtr config,
                     Network::TransportSocketPtr&& transport_socket)
    : config_(config), transport_socket_(std::move(transport_socket)) {}

void TapSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!tapper_);
  transport_socket_->setTransportSocketCallbacks(callbacks);
  tapper_ = config_ ? config_->createPerSocketTapper(callbacks.connection()) : nullptr;
}

std::string TapSocket::protocol() const { return transport_socket_->protocol(); }
absl::string_view TapSocket::failureReason() const { return transport_socket_->failureReason(); }

bool TapSocket::canFlushClose() { return transport_socket_->canFlushClose(); }

void TapSocket::closeSocket(Network::ConnectionEvent event) {
  if (tapper_ != nullptr) {
    tapper_->closeSocket(event);
  }

  transport_socket_->closeSocket(event);
}

Network::IoResult TapSocket::doRead(Buffer::Instance& buffer) {
  Network::IoResult result = transport_socket_->doRead(buffer);
  if (tapper_ != nullptr && result.bytes_processed_ > 0) {
    tapper_->onRead(buffer, result.bytes_processed_);
  }

  return result;
}

Network::IoResult TapSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  // TODO(htuch): avoid copy.
  Buffer::OwnedImpl copy(buffer);
  Network::IoResult result = transport_socket_->doWrite(buffer, end_stream);
  if (tapper_ != nullptr && result.bytes_processed_ > 0) {
    tapper_->onWrite(copy, result.bytes_processed_, end_stream);
  }
  return result;
}

void TapSocket::onConnected() { transport_socket_->onConnected(); }

Ssl::ConnectionInfoConstSharedPtr TapSocket::ssl() const { return transport_socket_->ssl(); }

TapSocketFactory::TapSocketFactory(
    const envoy::config::transport_socket::tap::v2alpha::Tap& proto_config,
    Common::Tap::TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
    Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_thread_dispatcher,
    Network::TransportSocketFactoryPtr&& transport_socket_factory)
    : ExtensionConfigBase(proto_config.common_config(), std::move(config_factory), admin,
                          singleton_manager, tls, main_thread_dispatcher),
      transport_socket_factory_(std::move(transport_socket_factory)) {}

Network::TransportSocketPtr
TapSocketFactory::createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const {
  return std::make_unique<TapSocket>(currentConfigHelper<SocketTapConfig>(),
                                     transport_socket_factory_->createTransportSocket(options));
}

bool TapSocketFactory::implementsSecureTransport() const {
  return transport_socket_factory_->implementsSecureTransport();
}

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
