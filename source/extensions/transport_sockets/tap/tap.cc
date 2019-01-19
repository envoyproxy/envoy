#include "extensions/transport_sockets/tap/tap.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

PerSocketTapperImpl::PerSocketTapperImpl(SocketTapConfigImplSharedPtr config,
                                         const Network::Connection& connection)
    : config_(std::move(config)), connection_(connection), statuses_(config_->numMatchers()),
      trace_(std::make_shared<envoy::data::tap::v2alpha::BufferedTraceWrapper>()) {
  config_->rootMatcher().onNewStream(statuses_);
}

void PerSocketTapperImpl::closeSocket(Network::ConnectionEvent) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return;
  }

  auto* connection = trace_->mutable_socket_buffered_trace()->mutable_connection();
  connection->set_id(connection_.id());
  Network::Utility::addressToProtobufAddress(*connection_.localAddress(),
                                             *connection->mutable_local_address());
  Network::Utility::addressToProtobufAddress(*connection_.remoteAddress(),
                                             *connection->mutable_remote_address());
  config_->sink().submitBufferedTrace(trace_, connection_.id());
}

void PerSocketTapperImpl::onRead(absl::string_view data) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return;
  }

  auto* event = trace_->mutable_socket_buffered_trace()->add_events();
  event->mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          config_->time_system_.systemTime().time_since_epoch())
          .count()));
  event->mutable_read()->set_data(data.data(), data.size());
}

void PerSocketTapperImpl::onWrite(absl::string_view data, bool end_stream) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return;
  }

  auto* event = trace_->mutable_socket_buffered_trace()->add_events();
  event->mutable_timestamp()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          config_->time_system_.systemTime().time_since_epoch())
          .count()));
  event->mutable_write()->set_data(data.data(), data.size());
  event->mutable_write()->set_end_stream(end_stream);
}

TapSocket::TapSocket(SocketTapConfigSharedPtr config,
                     Network::TransportSocketPtr&& transport_socket)
    : config_(config), transport_socket_(std::move(transport_socket)) {}

void TapSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  tapper_ = config_ ? config_->createPerSocketTapper(callbacks.connection()) : nullptr;
}

std::string TapSocket::protocol() const { return transport_socket_->protocol(); }

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
    // TODO(htuch): avoid linearizing
    const char* data = static_cast<const char*>(buffer.linearize(buffer.length())) +
                       (buffer.length() - result.bytes_processed_);
    tapper_->onRead(absl::string_view(data, result.bytes_processed_));
  }

  return result;
}

Network::IoResult TapSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  // TODO(htuch): avoid copy.
  Buffer::OwnedImpl copy(buffer);
  Network::IoResult result = transport_socket_->doWrite(buffer, end_stream);
  if (tapper_ != nullptr && result.bytes_processed_ > 0) {
    // TODO(htuch): avoid linearizing.
    const char* data = static_cast<const char*>(copy.linearize(result.bytes_processed_));
    tapper_->onWrite(absl::string_view(data, result.bytes_processed_), end_stream);
  }
  return result;
}

void TapSocket::onConnected() { transport_socket_->onConnected(); }

const Ssl::Connection* TapSocket::ssl() const { return transport_socket_->ssl(); }

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
TapSocketFactory::createTransportSocket(Network::TransportSocketOptionsSharedPtr) const {
  return std::make_unique<TapSocket>(currentConfigHelper<SocketTapConfig>(),
                                     transport_socket_factory_->createTransportSocket(nullptr));
}

bool TapSocketFactory::implementsSecureTransport() const {
  return transport_socket_factory_->implementsSecureTransport();
}

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
