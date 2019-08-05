#include "extensions/quic_listeners/quiche/active_quic_listener.h"

#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

ActiveQuicListener::ActiveQuicListener(Server::ConnectionHandlerImpl& parent,
                                       Network::ListenerConfig& listener_config)
    : ActiveQuicListener(parent,
                         parent.dispatcher_.createUdpListener(listener_config.socket(), *this),
                         listener_config) {}

ActiveQuicListener::ActiveQuicListener(Server::ConnectionHandlerImpl& parent,
                                       Network::ListenerPtr&& listener,
                                       Network::ListenerConfig& listener_config)
    : Server::ConnectionHandlerImpl::ActiveListenerBase(parent, std::move(listener),
                                                        listener_config),
      version_manager_(quic::CurrentSupportedVersions()) {
  quic::QuicRandom* const random = quic::QuicRandom::GetInstance();
  random->RandBytes(random_seed_, sizeof(random_seed_));
  crypto_config_ = std::make_unique<quic::QuicCryptoServerConfig>(
      quic::QuicStringPiece(reinterpret_cast<char*>(random_seed_), sizeof(random_seed_)),
      quic::QuicRandom::GetInstance(), std::make_unique<EnvoyQuicFakeProofSource>(),
      quic::KeyExchangeSource::Default());
  auto connection_helper = std::make_unique<EnvoyQuicConnectionHelper>(parent.dispatcher_);
  auto alarm_factory =
      std::make_unique<EnvoyQuicAlarmFactory>(parent.dispatcher_, *connection_helper->GetClock());
  quic_dispatcher_ = std::make_unique<EnvoyQuicDispatcher>(
      crypto_config_.get(), &version_manager_, std::move(connection_helper),
      std::move(alarm_factory), quic::kQuicDefaultConnectionIdLength, parent, config_, stats_);
  quic_dispatcher_->InitializeWithWriter(
      new EnvoyQuicPacketWriter(*dynamic_cast<Network::UdpListener*>(listener_.get())));
}

void ActiveQuicListener::onListenerShutdown() {
  ENVOY_LOG_TO_LOGGER(parent_.logger_, info, "Listener shutdown.");
  quic_dispatcher_->Shutdown();
}

void ActiveQuicListener::onData(Network::UdpRecvData& data) {
  quic::QuicSocketAddress peer_address(envoyAddressInstanceToQuicSocketAddress(data.peer_address_));
  quic::QuicSocketAddress self_address(
      envoyAddressInstanceToQuicSocketAddress(data.local_address_));
  quic::QuicTime timestamp =
      quic::QuicTime::Zero() +
      quic::QuicTime::Delta::FromMilliseconds(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  data.receive_time_.time_since_epoch())
                                                  .count());
  uint64_t num_slice = data.buffer_->getRawSlices(nullptr, 0);
  ASSERT(num_slice == 1);
  Buffer::RawSlice slice;
  data.buffer_->getRawSlices(&slice, 1);
  // TODO(danzh): pass in TTL and UDP header.
  quic::QuicReceivedPacket packet(reinterpret_cast<char*>(slice.mem_), slice.len_, timestamp,
                                  /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/true,
                                  /*packet_headers=*/nullptr, /*headers_length=*/0,
                                  /*owns_header_buffer*/ false);
  quic_dispatcher_->ProcessPacket(self_address, peer_address, packet);
}

void ActiveQuicListener::onWriteReady(const Network::Socket& /*socket*/) {
  quic_dispatcher_->OnCanWrite();
}

} // namespace Quic
} // namespace Envoy
