#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/common/assert.h"

#include "quiche/quic/core/quic_types.h"

namespace quic {

class QuicClock;
struct QuicConnectionStats;
class QuicRandom;
class QuicUnackedPacketMap;
class RttStats;
class SendAlgorithmInterface;

// Interface for creating a PCC SendAlgorithmInterface.
inline SendAlgorithmInterface*
CreatePccSenderImpl(const QuicClock* /*clock*/, const RttStats* /*rtt_stats*/,
                    const QuicUnackedPacketMap* /*unacked_packets*/, QuicRandom* /*random*/,
                    QuicConnectionStats* /*stats*/, QuicPacketCount /*initial_congestion_window*/,
                    QuicPacketCount /*max_congestion_window*/) {
  PANIC("PccSender is not supported.");
  return nullptr;
}

} // namespace quic
