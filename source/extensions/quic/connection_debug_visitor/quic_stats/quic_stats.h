#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/quic/connection_debug_visitor/quic_stats/v3/quic_stats.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"

#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionDebugVisitors {
namespace QuicStats {

#define ALL_QUIC_STATS(COUNTER, GAUGE, HISTOGRAM)                                                  \
  COUNTER(cx_tx_packets_total)                                                                     \
  COUNTER(cx_tx_packets_retransmitted_total)                                                       \
  COUNTER(cx_tx_amplification_throttling_total)                                                    \
  COUNTER(cx_rx_packets_total)                                                                     \
  COUNTER(cx_path_degrading_total)                                                                 \
  COUNTER(cx_forward_progress_after_path_degrading_total)                                          \
  HISTOGRAM(cx_rtt_us, Microseconds)                                                               \
  HISTOGRAM(cx_tx_estimated_bandwidth, Bytes)                                                      \
  HISTOGRAM(cx_tx_percent_retransmitted_packets, Percent)                                          \
  HISTOGRAM(cx_tx_mtu, Bytes)                                                                      \
  HISTOGRAM(cx_rx_mtu, Bytes)

struct QuicStats {
  ALL_QUIC_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class Config;

// Visitor class that publishes various QUICHE connection stats as Envoy stats.
class QuicStatsVisitor : public quic::QuicConnectionDebugVisitor {
public:
  QuicStatsVisitor(Config& config, Event::Dispatcher& dispatcher);

  // quic::QuicConnectionDebugVisitor
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;

  // This is virtual so that tests can override it.
  virtual const quic::QuicConnectionStats& getQuicStats() PURE;

private:
  void recordStats();

  Config& config_;
  Event::TimerPtr timer_;

  quic::QuicPacketCount last_packets_sent_{};
  quic::QuicPacketCount last_packets_retransmitted_{};
  quic::QuicPacketCount last_packets_received_{};
  size_t last_num_amplification_throttling_{};
  size_t last_num_path_degrading_{};
  size_t last_num_forward_progress_after_path_degrading_{};
};

class QuicStatsVisitorProd final : public QuicStatsVisitor {
public:
  QuicStatsVisitorProd(Config& config, Event::Dispatcher& dispatcher, quic::QuicSession& session)
      : QuicStatsVisitor(config, dispatcher), session_(session) {}

  const quic::QuicConnectionStats& getQuicStats() override;

private:
  quic::QuicSession& session_;
};

class Config : public Envoy::Quic::EnvoyQuicConnectionDebugVisitorFactoryInterface {
public:
  Config(const envoy::extensions::quic::connection_debug_visitor::quic_stats::v3::Config& config,
         Stats::Scope& scope);

  std::unique_ptr<quic::QuicConnectionDebugVisitor>
  createQuicConnectionDebugVisitor(Event::Dispatcher& dispatcher, quic::QuicSession& session,
                                   const StreamInfo::StreamInfo& stream_info) override;

  const absl::optional<std::chrono::milliseconds> update_period_;
  QuicStats stats_;

private:
  static QuicStats generateStats(Stats::Scope& scope);
};

class QuicStatsFactoryFactory
    : public Envoy::Quic::EnvoyQuicConnectionDebugVisitorFactoryFactoryInterface {
public:
  std::string name() const override { return "envoy.quic.connection_debug_visitor.quic_stats"; }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::quic::connection_debug_visitor::quic_stats::v3::Config>();
  }

  Envoy::Quic::EnvoyQuicConnectionDebugVisitorFactoryInterfacePtr
  createFactory(const Protobuf::Message& config,
                Server::Configuration::ListenerFactoryContext& listener_context) override;
};

DECLARE_FACTORY(QuicStatsFactoryFactory);

} // namespace QuicStats
} // namespace ConnectionDebugVisitors
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
