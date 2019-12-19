#include "extensions/quic_listeners/quiche/active_quic_listener.h"

#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

ActiveQuicListener::ActiveQuicListener(Event::Dispatcher& dispatcher,
                                       Network::ConnectionHandler& parent,
                                       Network::ListenerConfig& listener_config,
                                       const quic::QuicConfig& quic_config,
                                       Network::Socket::OptionsSharedPtr options)
    : ActiveQuicListener(dispatcher, parent,
                         listener_config.listenSocketFactory().getListenSocket(), listener_config,
                         quic_config, std::move(options)) {}

ActiveQuicListener::ActiveQuicListener(Event::Dispatcher& dispatcher,
                                       Network::ConnectionHandler& parent,
                                       Network::SocketSharedPtr listen_socket,
                                       Network::ListenerConfig& listener_config,
                                       const quic::QuicConfig& quic_config,
                                       Network::Socket::OptionsSharedPtr options)
    : Server::ConnectionHandlerImpl::ActiveListenerImplBase(parent, listener_config),
      dispatcher_(dispatcher), version_manager_(quic::CurrentSupportedVersions()),
      listen_socket_(*listen_socket) {
  if (options != nullptr) {
    const bool ok = Network::Socket::applyOptions(options, listen_socket_,
                                                  envoy::api::v2::core::SocketOption::STATE_BOUND);
    if (!ok) {
      ENVOY_LOG(warn, "Fail to apply socket options to socket {} on listener {} after binding",
                listen_socket_.ioHandle().fd(), listener_config.name());
      throw EnvoyException("Fail to apply socket options.");
    }
    listen_socket_.addOptions(options);
  }

  udp_listener_ = dispatcher_.createUdpListener(std::move(listen_socket), *this);
  quic::QuicRandom* const random = quic::QuicRandom::GetInstance();
  random->RandBytes(random_seed_, sizeof(random_seed_));
  crypto_config_ = std::make_unique<quic::QuicCryptoServerConfig>(
      quic::QuicStringPiece(reinterpret_cast<char*>(random_seed_), sizeof(random_seed_)),
      quic::QuicRandom::GetInstance(), std::make_unique<EnvoyQuicFakeProofSource>(),
      quic::KeyExchangeSource::Default());
  auto connection_helper = std::make_unique<EnvoyQuicConnectionHelper>(dispatcher_);
  crypto_config_->AddDefaultConfig(random, connection_helper->GetClock(),
                                   quic::QuicCryptoServerConfig::ConfigOptions());
  auto alarm_factory =
      std::make_unique<EnvoyQuicAlarmFactory>(dispatcher_, *connection_helper->GetClock());
  quic_dispatcher_ = std::make_unique<EnvoyQuicDispatcher>(
      crypto_config_.get(), quic_config, &version_manager_, std::move(connection_helper),
      std::move(alarm_factory), quic::kQuicDefaultConnectionIdLength, parent, config_, stats_,
      per_worker_stats_, dispatcher, listen_socket_);
  quic_dispatcher_->InitializeWithWriter(new EnvoyQuicPacketWriter(listen_socket_));
}

ActiveQuicListener::~ActiveQuicListener() { onListenerShutdown(); }

void ActiveQuicListener::onListenerShutdown() {
  ENVOY_LOG(info, "Quic listener {} shutdown.", config_.name());
  quic_dispatcher_->Shutdown();
}

void ActiveQuicListener::onData(Network::UdpRecvData& data) {
  quic::QuicSocketAddress peer_address(
      envoyAddressInstanceToQuicSocketAddress(data.addresses_.peer_));
  quic::QuicSocketAddress self_address(
      envoyAddressInstanceToQuicSocketAddress(data.addresses_.local_));
  quic::QuicTime timestamp =
      quic::QuicTime::Zero() +
      quic::QuicTime::Delta::FromMicroseconds(std::chrono::duration_cast<std::chrono::microseconds>(
                                                  data.receive_time_.time_since_epoch())
                                                  .count());
  uint64_t num_slice = data.buffer_->getRawSlices(nullptr, 0);
  ASSERT(num_slice == 1);
  Buffer::RawSlice slice;
  data.buffer_->getRawSlices(&slice, 1);
  // TODO(danzh): pass in TTL and UDP header.
  quic::QuicReceivedPacket packet(reinterpret_cast<char*>(slice.mem_), slice.len_, timestamp,
                                  /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/false,
                                  /*packet_headers=*/nullptr, /*headers_length=*/0,
                                  /*owns_header_buffer*/ false);
  quic_dispatcher_->ProcessPacket(self_address, peer_address, packet);
}

void ActiveQuicListener::onWriteReady(const Network::Socket& /*socket*/) {
  quic_dispatcher_->OnCanWrite();
}

} // namespace Quic
} // namespace Envoy
