#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_network_connection.h"
#include "source/common/quic/envoy_quic_packet_writer.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_connection.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Quic {

class PacketsToReadDelegate {
public:
  virtual ~PacketsToReadDelegate() = default;

  virtual size_t numPacketsExpectedPerEventLoop() PURE;
};

// A client QuicConnection instance managing its own file events.
class EnvoyQuicClientConnection : public quic::QuicConnection,
                                  public QuicNetworkConnection,
                                  public Network::UdpPacketProcessor {
public:

  class EnvoyQuicPathValidationContext : public quic::QuicPathValidationContext {
  public:
    EnvoyQuicPathValidationContext(quic::QuicSocketAddress& self_address, quic::QuicSocketAddress& peer_address, 
      Network::ConnectionSocketPtr connectionSocket, std::unique_ptr<EnvoyQuicPacketWriter> writer);

    ~EnvoyQuicPathValidationContext() override;

    quic::QuicPacketWriter* WriterToUse() override;

    std::unique_ptr<EnvoyQuicPacketWriter> ReleaseWriter();
    Network::ConnectionSocketPtr ReleaseSocket();


  private:
    Network::ConnectionSocketPtr socket_;
    std::unique_ptr<EnvoyQuicPacketWriter> writer_;
  };

  class EnvoyPathValidationResultDelegate : public quic::QuicPathValidator::ResultDelegate {
  public:
    explicit EnvoyPathValidationResultDelegate(EnvoyQuicClientConnection& connection);

    void OnPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context) override;

    void OnPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> context) override;

  private:
    EnvoyQuicClientConnection& connection_;
  };


  // A connection socket will be created with given |local_addr|. If binding
  // port not provided in |local_addr|, pick up a random port.
  EnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                            Network::Address::InstanceConstSharedPtr& initial_peer_address,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Network::Address::InstanceConstSharedPtr local_addr,
                            Event::Dispatcher& dispatcher,
                            const Network::ConnectionSocket::OptionsSharedPtr& options);

  EnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer,
                            bool owns_writer,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Event::Dispatcher& dispatcher,
                            Network::ConnectionSocketPtr&& connection_socket);

  // Network::UdpPacketProcessor
  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time) override;
  uint64_t maxDatagramSize() const override;
  void onDatagramsDropped(uint32_t) override {
    // TODO(mattklein123): Emit a stat for this.
  }
  size_t numPacketsExpectedPerEventLoop() const override {
    if (delegate_.has_value()) {
      return delegate_.value().get().numPacketsExpectedPerEventLoop();
    }
    return DEFAULT_PACKETS_TO_READ_PER_CONNECTION;
  }

  // Register file event and apply socket options.
  void setUpConnectionSocket(Network::ConnectionSocket& connection_socket, OptRef<PacketsToReadDelegate> delegate);

  // Switch underlying socket with the given one. This is used in connection migration.
  void switchConnectionSocket(Network::ConnectionSocketPtr&& connection_socket);

  // Potentially trigger migration.
  void OnPathDegradingDetected() override;

  void OnPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context);

  void OnPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> context);

private:
  EnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Event::Dispatcher& dispatcher,
                            Network::ConnectionSocketPtr&& connection_socket);

  void onFileEvent(uint32_t events);

  void MaybeMigratePort();

  OptRef<PacketsToReadDelegate> delegate_;
  uint32_t packets_dropped_{0};
  Event::Dispatcher& dispatcher_;
};

} // namespace Quic
} // namespace Envoy
