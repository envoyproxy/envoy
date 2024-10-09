#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_network_connection.h"

#include "quiche/quic/core/quic_connection.h"

namespace Envoy {
namespace Quic {

// Limits the max number of sockets created.
constexpr uint8_t kMaxNumSocketSwitches = 5;

class PacketsToReadDelegate {
public:
  virtual ~PacketsToReadDelegate() = default;

  virtual size_t numPacketsExpectedPerEventLoop() const PURE;
};

// A client QuicConnection instance managing its own file events.
class EnvoyQuicClientConnection : public quic::QuicConnection,
                                  public QuicNetworkConnection,
                                  public Network::UdpPacketProcessor {
public:
  // Holds all components needed for a QUIC connection probing/migration.
  class EnvoyQuicPathValidationContext : public quic::QuicPathValidationContext {
  public:
    EnvoyQuicPathValidationContext(const quic::QuicSocketAddress& self_address,
                                   const quic::QuicSocketAddress& peer_address,
                                   std::unique_ptr<EnvoyQuicPacketWriter> writer,
                                   std::unique_ptr<Network::ConnectionSocket> probing_socket);

    ~EnvoyQuicPathValidationContext() override;

    quic::QuicPacketWriter* WriterToUse() override;

    EnvoyQuicPacketWriter* releaseWriter();

    Network::ConnectionSocket& probingSocket();

    std::unique_ptr<Network::ConnectionSocket> releaseSocket();

  private:
    std::unique_ptr<EnvoyQuicPacketWriter> writer_;
    Network::ConnectionSocketPtr socket_;
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
                            const Network::ConnectionSocket::OptionsSharedPtr& options,
                            quic::ConnectionIdGeneratorInterface& generator, bool prefer_gro);

  EnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer,
                            bool owns_writer,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Event::Dispatcher& dispatcher,
                            Network::ConnectionSocketPtr&& connection_socket,
                            quic::ConnectionIdGeneratorInterface& generator, bool prefer_gro);

  // Network::UdpPacketProcessor
  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                     Buffer::RawSlice saved_cmsg) override;
  uint64_t maxDatagramSize() const override;
  void onDatagramsDropped(uint32_t) override {
    // TODO(mattklein123): Emit a stat for this.
  }
  size_t numPacketsExpectedPerEventLoop() const override {
    if (!Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.quic_upstream_reads_fixed_number_packets") &&
        delegate_.has_value()) {
      return delegate_->numPacketsExpectedPerEventLoop();
    }
    return DEFAULT_PACKETS_TO_READ_PER_CONNECTION;
  }
  const Network::IoHandle::UdpSaveCmsgConfig& saveCmsgConfig() const override {
    static const Network::IoHandle::UdpSaveCmsgConfig empty_config{};
    return empty_config;
  };

  // Register file event and apply socket options.
  void setUpConnectionSocket(Network::ConnectionSocket& connection_socket,
                             OptRef<PacketsToReadDelegate> delegate);

  // Switch underlying socket with the given one. This is used in connection migration.
  void switchConnectionSocket(Network::ConnectionSocketPtr&& connection_socket);

  // quic::QuicConnection
  // Potentially trigger migration.
  void OnPathDegradingDetected() override;
  void OnCanWrite() override;

  // Called when port migration probing succeeds. Attempts to migrate this connection onto the new
  // socket extracted from context.
  void onPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context);

  // Called when port migration probing fails. The probing socket from context will go out of scope
  // and be destructed.
  void onPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> context);

  void setNumPtosForPortMigration(uint32_t num_ptos_for_path_degrading);

  // Probes the given peer address. If the probing succeeds, start sending packets to this address
  // instead.
  void
  probeAndMigrateToServerPreferredAddress(const quic::QuicSocketAddress& server_preferred_address);

private:
  friend class EnvoyQuicClientConnectionPeer;

  // Receives notifications from the Quiche layer on path validation results.
  class EnvoyPathValidationResultDelegate : public quic::QuicPathValidator::ResultDelegate {
  public:
    explicit EnvoyPathValidationResultDelegate(EnvoyQuicClientConnection& connection);

    void OnPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context,
                                 quic::QuicTime start_time) override;

    void OnPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> context) override;

  private:
    EnvoyQuicClientConnection& connection_;
  };
  EnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Event::Dispatcher& dispatcher,
                            Network::ConnectionSocketPtr&& connection_socket,
                            quic::ConnectionIdGeneratorInterface& generator, bool prefer_gro);

  void onFileEvent(uint32_t events, Network::ConnectionSocket& connection_socket);

  void maybeMigratePort();

  void probeWithNewPort(const quic::QuicSocketAddress& peer_addr,
                        quic::PathValidationReason reason);

  OptRef<PacketsToReadDelegate> delegate_;
  uint32_t packets_dropped_{0};
  Event::Dispatcher& dispatcher_;
  bool migrate_port_on_path_degrading_{false};
  uint8_t num_socket_switches_{0};
  size_t num_packets_with_unknown_dst_address_{0};
  const bool prefer_gro_;
  const bool disallow_mmsg_;
};

} // namespace Quic
} // namespace Envoy
