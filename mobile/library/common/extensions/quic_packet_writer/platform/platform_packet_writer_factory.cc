#include "library/common/extensions/quic_packet_writer/platform/platform_packet_writer_factory.h"

#include <chrono>
#include <iostream>

#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "library/common/system/system_helper.h"

namespace Envoy {
namespace Quic {

namespace {

// Max retries for ENOBUFS error. Same as Chromium value
// (https://source.chromium.org/chromium/chromium/src/+/main:net/quic/quic_chromium_packet_writer.cc;l=34;bpv=1;bpt=0).
constexpr size_t MaxRetries = 12;

class RetriablePacketWriter : public Network::UdpDefaultWriter {
public:
  RetriablePacketWriter(Event::Dispatcher& dispatcher, Network::IoHandle& io_handle)
      : Network::UdpDefaultWriter(io_handle), dispatcher_(dispatcher) {}

  Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                      const Network::Address::Ip* local_ip,
                                      const Network::Address::Instance& peer_address) override {
    Api::IoCallUint64Result result =
        Network::UdpDefaultWriter::writePacket(buffer, local_ip, peer_address);
    if (result.ok()) {
      // Reset retry count on successful write.
      retry_count_ = 0;
    } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::NoBufferSpace &&
               retry_count_ < MaxRetries) {
      // Override the error to EAGAIN and treat it as a write block signal.
      ENVOY_LOG_MISC(debug, "Encountered ENOBUFS, will retry");
      setBlocked();
      if (retry_timer_ == nullptr) {
        retry_timer_ = dispatcher_.createTimer([this]() {
          // On timer fire, re-arm a write event to retry sending packets.
          ioHandle().activateFileEvents(Event::FileReadyType::Write);
        });
      }
      // Exponential backoff for retries.
      retry_timer_->enableTimer(std::chrono::milliseconds(1 << retry_count_));
      ++retry_count_;
      return {0, Network::IoSocketError::getIoSocketEagainError()};
    }
    return result;
  }

private:
  Event::Dispatcher& dispatcher_;
  Event::TimerPtr retry_timer_;
  size_t retry_count_{0};
};

} // namespace

QuicClientPacketWriterFactory::CreationResult
QuicPlatformPacketWriterFactory::createSocketAndQuicPacketWriter(
    Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle network,
    Network::Address::InstanceConstSharedPtr& local_addr,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  auto custom_bind_func = [](Network::ConnectionSocket& socket, quic::QuicNetworkHandle net) {
    SystemHelper::getInstance().bindSocketToNetwork(socket, net);
  };

  Network::ConnectionSocketPtr connection_socket =
      createConnectionSocket(server_addr, local_addr, options, network, custom_bind_func);

  Network::IoHandle& io_handle = connection_socket->ioHandle();
  return {std::make_unique<EnvoyQuicPacketWriter>(
              std::make_unique<RetriablePacketWriter>(dispatcher_, io_handle)),
          std::move(connection_socket)};
}

} // namespace Quic
} // namespace Envoy
