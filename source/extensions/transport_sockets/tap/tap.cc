#include "source/extensions/transport_sockets/tap/tap.h"

#include "envoy/extensions/transport_sockets/tap/v3/tap.pb.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

TapSocket::TapSocket(SocketTapConfigSharedPtr config,
                     Network::TransportSocketPtr&& transport_socket)
    : PassthroughSocket(std::move(transport_socket)), config_(config) {}

void TapSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  ASSERT(!tapper_);
  transport_socket_->setTransportSocketCallbacks(callbacks);
  tapper_ = config_ ? config_->createPerSocketTapper(callbacks.connection()) : nullptr;
}

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

TapSocketFactory::TapSocketFactory(
    const envoy::extensions::transport_sockets::tap::v3::Tap& proto_config,
    Common::Tap::TapConfigFactoryPtr&& config_factory, OptRef<Server::Admin> admin,
    Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_thread_dispatcher,
    Network::UpstreamTransportSocketFactoryPtr&& transport_socket_factory)
    : ExtensionConfigBase(proto_config.common_config(), std::move(config_factory), admin,
                          singleton_manager, tls, main_thread_dispatcher),
      PassthroughFactory(std::move(transport_socket_factory)) {}

Network::TransportSocketPtr
TapSocketFactory::createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                                        Upstream::HostDescriptionConstSharedPtr host) const {
  return std::make_unique<TapSocket>(
      currentConfigHelper<SocketTapConfig>(),
      transport_socket_factory_->createTransportSocket(options, host));
}

DownstreamTapSocketFactory::DownstreamTapSocketFactory(
    const envoy::extensions::transport_sockets::tap::v3::Tap& proto_config,
    Common::Tap::TapConfigFactoryPtr&& config_factory, OptRef<Server::Admin> admin,
    Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_thread_dispatcher,
    Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory)
    : ExtensionConfigBase(proto_config.common_config(), std::move(config_factory), admin,
                          singleton_manager, tls, main_thread_dispatcher),
      DownstreamPassthroughFactory(std::move(transport_socket_factory)) {}

Network::TransportSocketPtr DownstreamTapSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<TapSocket>(currentConfigHelper<SocketTapConfig>(),
                                     transport_socket_factory_->createDownstreamTransportSocket());
}

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
