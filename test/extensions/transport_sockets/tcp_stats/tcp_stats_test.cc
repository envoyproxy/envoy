#if defined(__linux__)
#define DO_NOT_INCLUDE_NETINET_TCP_H 1

#include <linux/tcp.h>

#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.h"

#include "source/extensions/transport_sockets/tcp_stats/config.h"
#include "source/extensions/transport_sockets/tcp_stats/tcp_stats.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/transport_socket_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Return;
using testing::ReturnNull;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {
namespace {

class TcpStatsTest : public testing::Test {
public:
  void initialize(bool enable_periodic) {
    envoy::extensions::transport_sockets::tcp_stats::v3::Config proto_config;
    if (enable_periodic) {
      proto_config.mutable_update_period()->MergeFrom(
          ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    }
    config_ = std::make_shared<Config>(proto_config, *store_.rootScope());
    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    ON_CALL(io_handle_, getOption(IPPROTO_TCP, TCP_INFO, _, _))
        .WillByDefault(Invoke([this](int, int, void* optval, socklen_t* optlen) {
          ASSERT(*optlen == sizeof(tcp_info_));
          memcpy(optval, &tcp_info_, sizeof(tcp_info_));
          return Api::SysCallIntResult{0, 0};
        }));
    createTcpStatsSocket(enable_periodic, timer_, inner_socket_, tcp_stats_socket_);
  }

  void createTcpStatsSocket(bool enable_periodic, NiceMock<Event::MockTimer>*& timer,
                            NiceMock<Network::MockTransportSocket>*& inner_socket_out,
                            std::unique_ptr<TcpStatsSocket>& tcp_stats_socket) {
    if (enable_periodic) {
      timer = new NiceMock<Event::MockTimer>(&transport_callbacks_.connection_.dispatcher_);
      EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(1000), _)).Times(AtLeast(1));
    }
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_out = inner_socket.get();
    tcp_stats_socket = std::make_unique<TcpStatsSocket>(config_, std::move(inner_socket));
    tcp_stats_socket->setTransportSocketCallbacks(transport_callbacks_);
    tcp_stats_socket->onConnected();
  }

  uint64_t counterValue(absl::string_view name) {
    auto opt_ref = store_.findCounterByString(absl::StrCat("tcp_stats.", name));
    ASSERT(opt_ref.has_value());
    return opt_ref.value().get().value();
  }

  int64_t gaugeValue(absl::string_view name) {
    auto opt_ref = store_.findGaugeByString(absl::StrCat("tcp_stats.", name));
    ASSERT(opt_ref.has_value());
    return opt_ref.value().get().value();
  }

  absl::optional<uint64_t> histogramValue(absl::string_view name) {
    std::vector<uint64_t> values = store_.histogramValues(absl::StrCat("tcp_stats.", name), true);
    ASSERT(values.size() <= 1,
           absl::StrCat(name, " didn't have <=1 value, instead had ", values.size()));
    if (values.empty()) {
      return absl::nullopt;
    } else {
      return values[0];
    }
  }

  Stats::TestUtil::TestStore store_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  NiceMock<Network::MockIoHandle> io_handle_;
  std::shared_ptr<Config> config_;
  std::unique_ptr<TcpStatsSocket> tcp_stats_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
  NiceMock<Event::MockTimer>* timer_;
  struct tcp_info tcp_info_;
};

// Validate that the configured update_period is honored, and that stats are updated when the timer
// fires.
TEST_F(TcpStatsTest, Periodic) {
  initialize(true);

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000), _));
  tcp_info_.tcpi_notsent_bytes = 42;
  timer_->callback_();
  EXPECT_EQ(42, gaugeValue("cx_tx_unsent_bytes"));

  EXPECT_CALL(*timer_, disableTimer());
  tcp_stats_socket_->closeSocket(Network::ConnectionEvent::RemoteClose);
}

// Validate that stats are updated when the connection is closed. Gauges should be set to zero,
// and counters should be appropriately updated.
TEST_F(TcpStatsTest, CloseSocket) {
  initialize(false);

  tcp_info_.tcpi_segs_out = 42;
  tcp_info_.tcpi_notsent_bytes = 1;
  tcp_info_.tcpi_unacked = 2;
  EXPECT_CALL(*inner_socket_, closeSocket(Network::ConnectionEvent::RemoteClose));
  tcp_stats_socket_->closeSocket(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(42, counterValue("cx_tx_segments"));
  EXPECT_EQ(0, gaugeValue("cx_tx_unsent_bytes"));
  EXPECT_EQ(0, gaugeValue("cx_tx_unacked_segments"));
}

TEST_F(TcpStatsTest, SyscallFailureReturnCode) {
  initialize(true);
  tcp_info_.tcpi_notsent_bytes = 42;
  EXPECT_CALL(io_handle_, getOption(IPPROTO_TCP, TCP_INFO, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, 42}));
  EXPECT_LOG_CONTAINS(
      "debug",
      fmt::format("Failed getsockopt(IPPROTO_TCP, TCP_INFO): rc -1 errno 42 optlen {}",
                  sizeof(tcp_info_)),
      timer_->callback_());

  // Not updated on failed syscall.
  EXPECT_EQ(0, gaugeValue("cx_tx_unsent_bytes"));
}

// Validate that the emitted values are correct, that delta updates from a counter move the value by
// the delta (not the entire value), and that multiple sockets interact correctly (stats are
// summed).
TEST_F(TcpStatsTest, Values) {
  initialize(true);

  NiceMock<Event::MockTimer>* timer2;
  NiceMock<Network::MockTransportSocket>* inner_socket2;
  std::unique_ptr<TcpStatsSocket> tcp_stats_socket2;
  createTcpStatsSocket(true, timer2, inner_socket2, tcp_stats_socket2);

  // After the first call, stats should be set to exactly these values.
  tcp_info_.tcpi_total_retrans = 1;
  tcp_info_.tcpi_segs_out = 2;
  tcp_info_.tcpi_segs_in = 3;
  tcp_info_.tcpi_data_segs_out = 4;
  tcp_info_.tcpi_data_segs_in = 5;
  tcp_info_.tcpi_notsent_bytes = 6;
  tcp_info_.tcpi_unacked = 7;
  tcp_info_.tcpi_rtt = 8;
  tcp_info_.tcpi_rttvar = 9;
  timer_->callback_();
  EXPECT_EQ(1, counterValue("cx_tx_retransmitted_segments"));
  EXPECT_EQ(2, counterValue("cx_tx_segments"));
  EXPECT_EQ(3, counterValue("cx_rx_segments"));
  EXPECT_EQ(4, counterValue("cx_tx_data_segments"));
  EXPECT_EQ(5, counterValue("cx_rx_data_segments"));
  EXPECT_EQ(6, gaugeValue("cx_tx_unsent_bytes"));
  EXPECT_EQ(7, gaugeValue("cx_tx_unacked_segments"));
  EXPECT_EQ(8U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(9U, histogramValue("cx_rtt_variance_us"));
  EXPECT_EQ((1U * Stats::Histogram::PercentScale) / 4U,
            histogramValue("cx_tx_percent_retransmitted_segments"));

  // Trigger the timer again with unchanged values. The metrics should be unchanged (but the
  // histograms should have emitted the value again).
  timer_->callback_();
  EXPECT_EQ(1, counterValue("cx_tx_retransmitted_segments"));
  EXPECT_EQ(2, counterValue("cx_tx_segments"));
  EXPECT_EQ(3, counterValue("cx_rx_segments"));
  EXPECT_EQ(4, counterValue("cx_tx_data_segments"));
  EXPECT_EQ(5, counterValue("cx_rx_data_segments"));
  EXPECT_EQ(6, gaugeValue("cx_tx_unsent_bytes"));
  EXPECT_EQ(7, gaugeValue("cx_tx_unacked_segments"));
  EXPECT_EQ(8U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(9U, histogramValue("cx_rtt_variance_us"));
  // No more packets were transmitted (numerator and denominator deltas are zero), so no value
  // should be emitted.
  EXPECT_EQ(absl::nullopt, histogramValue("cx_tx_percent_retransmitted_segments"));

  // Set stats on 2nd socket. Values should be combined.
  tcp_info_.tcpi_total_retrans = 1;
  tcp_info_.tcpi_segs_out = 1;
  tcp_info_.tcpi_segs_in = 1;
  tcp_info_.tcpi_data_segs_out = 1;
  tcp_info_.tcpi_data_segs_in = 1;
  tcp_info_.tcpi_notsent_bytes = 1;
  tcp_info_.tcpi_unacked = 1;
  tcp_info_.tcpi_rtt = 1;
  tcp_info_.tcpi_rttvar = 1;
  timer2->callback_();
  EXPECT_EQ(2, counterValue("cx_tx_retransmitted_segments"));
  EXPECT_EQ(3, counterValue("cx_tx_segments"));
  EXPECT_EQ(4, counterValue("cx_rx_segments"));
  EXPECT_EQ(5, counterValue("cx_tx_data_segments"));
  EXPECT_EQ(6, counterValue("cx_rx_data_segments"));
  EXPECT_EQ(7, gaugeValue("cx_tx_unsent_bytes"));
  EXPECT_EQ(8, gaugeValue("cx_tx_unacked_segments"));
  EXPECT_EQ(1U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(1U, histogramValue("cx_rtt_variance_us"));
  EXPECT_EQ(Stats::Histogram::PercentScale /* 100% */,
            histogramValue("cx_tx_percent_retransmitted_segments"));

  // Update the first socket again.
  tcp_info_.tcpi_total_retrans = 2;
  tcp_info_.tcpi_segs_out = 3;
  tcp_info_.tcpi_segs_in = 4;
  tcp_info_.tcpi_data_segs_out = 5;
  tcp_info_.tcpi_data_segs_in = 6;
  tcp_info_.tcpi_notsent_bytes = 7;
  tcp_info_.tcpi_unacked = 8;
  tcp_info_.tcpi_rtt = 9;
  tcp_info_.tcpi_rttvar = 10;
  timer_->callback_();
  EXPECT_EQ(3, counterValue("cx_tx_retransmitted_segments"));
  EXPECT_EQ(4, counterValue("cx_tx_segments"));
  EXPECT_EQ(5, counterValue("cx_rx_segments"));
  EXPECT_EQ(6, counterValue("cx_tx_data_segments"));
  EXPECT_EQ(7, counterValue("cx_rx_data_segments"));
  EXPECT_EQ(8, gaugeValue("cx_tx_unsent_bytes"));
  EXPECT_EQ(9, gaugeValue("cx_tx_unacked_segments"));
  EXPECT_EQ(9U, histogramValue("cx_rtt_us"));
  EXPECT_EQ(10U, histogramValue("cx_rtt_variance_us"));
  // Delta of 1 on numerator and denominator.
  EXPECT_EQ(Stats::Histogram::PercentScale /* 100% */,
            histogramValue("cx_tx_percent_retransmitted_segments"));
}

class TcpStatsSocketFactoryTest : public testing::Test {
public:
  void initialize() {
    envoy::extensions::transport_sockets::tcp_stats::v3::Config proto_config;
    auto inner_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
    inner_factory_ = inner_factory.get();
    factory_ = std::make_unique<UpstreamTcpStatsSocketFactory>(context_, proto_config,
                                                               std::move(inner_factory));
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  NiceMock<Network::MockTransportSocketFactory>* inner_factory_;
  std::unique_ptr<UpstreamTcpStatsSocketFactory> factory_;
};

// Test createTransportSocket returns nullptr if inner call returns nullptr
TEST_F(TcpStatsSocketFactoryTest, CreateSocketReturnsNullWhenInnerFactoryReturnsNull) {
  initialize();
  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _)).WillOnce(ReturnNull());
  EXPECT_EQ(nullptr, factory_->createTransportSocket(nullptr, nullptr));
}

// Test implementsSecureTransport calls inner factory
TEST_F(TcpStatsSocketFactoryTest, ImplementsSecureTransportCallInnerFactory) {
  initialize();
  EXPECT_CALL(*inner_factory_, implementsSecureTransport()).WillOnce(Return(true));
  EXPECT_TRUE(factory_->implementsSecureTransport());

  EXPECT_CALL(*inner_factory_, implementsSecureTransport()).WillOnce(Return(false));
  EXPECT_FALSE(factory_->implementsSecureTransport());
}

} // namespace
} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#else // #if defined(__linux__)

#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.h"

#include "test/mocks/server/transport_socket_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {

TEST(TcpStatsTest, ConfigErrorOnUnsupportedPlatform) {
  envoy::extensions::transport_sockets::tcp_stats::v3::Config proto_config;
  proto_config.mutable_transport_socket()->set_name("envoy.transport_sockets.raw_buffer");
  envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
  proto_config.mutable_transport_socket()->mutable_typed_config()->PackFrom(raw_buffer);
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context;

  envoy::config::core::v3::TransportSocket transport_socket_config;
  transport_socket_config.set_name("envoy.transport_sockets.tcp_stats");
  transport_socket_config.mutable_typed_config()->PackFrom(proto_config);
  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(transport_socket_config);
  EXPECT_THROW_WITH_MESSAGE(
      config_factory.createTransportSocketFactory(proto_config, context, {}).value(),
      EnvoyException, "envoy.transport_sockets.tcp_stats is not supported on this platform.");
}

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#endif // #if defined(__linux__)
