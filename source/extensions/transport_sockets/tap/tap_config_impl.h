#pragma once

#include "envoy/event/timer.h"

#include "extensions/common/tap/tap_config_base.h"
#include "extensions/transport_sockets/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

class SocketTapConfigImpl;
using SocketTapConfigImplSharedPtr = std::shared_ptr<SocketTapConfigImpl>;

class PerSocketTapperImpl : public PerSocketTapper {
public:
  PerSocketTapperImpl(SocketTapConfigImplSharedPtr config, const Network::Connection& connection);

  // PerSocketTapper
  void closeSocket(Network::ConnectionEvent event) override;
  void onRead(const Buffer::Instance& data, uint32_t bytes_read) override;
  void onWrite(const Buffer::Instance& data, uint32_t bytes_written, bool end_stream) override;

private:
  envoy::data::tap::v2alpha::SocketEvent& createEvent();

  SocketTapConfigImplSharedPtr config_;
  const Network::Connection& connection_;
  std::vector<bool> statuses_;
  std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper> trace_;
  uint32_t rx_bytes_buffered_{};
  uint32_t tx_bytes_buffered_{};
};

class SocketTapConfigImpl : public Extensions::Common::Tap::TapConfigBaseImpl,
                            public SocketTapConfig,
                            public std::enable_shared_from_this<SocketTapConfigImpl> {
public:
  SocketTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                      Extensions::Common::Tap::Sink* admin_streamer, TimeSource& time_system)
      : Extensions::Common::Tap::TapConfigBaseImpl(std::move(proto_config), admin_streamer),
        time_source_(time_system) {}

  // SocketTapConfig
  PerSocketTapperPtr createPerSocketTapper(const Network::Connection& connection) override {
    return std::make_unique<PerSocketTapperImpl>(shared_from_this(), connection);
  }

private:
  TimeSource& time_source_;

  friend class PerSocketTapperImpl;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
