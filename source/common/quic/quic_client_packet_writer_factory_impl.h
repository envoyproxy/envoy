#pragma once

#include "source/common/quic/envoy_quic_client_connection.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Quic {

class QuicClientPacketWriterFactoryImpl : public QuicClientPacketWriterFactory {
public:
  absl::StatusOr<CreationResult> createSocketAndQuicPacketWriter(
      Network::Address::InstanceConstSharedPtr server_addr, quic::QuicNetworkHandle /*network*/,
      Network::Address::InstanceConstSharedPtr& local_addr,
      const Network::ConnectionSocket::OptionsSharedPtr& options) override;
};

} // namespace Quic
} // namespace Envoy
