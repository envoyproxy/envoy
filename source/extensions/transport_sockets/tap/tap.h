#pragma once

#include "envoy/config/transport_socket/tap/v2alpha/tap.pb.h"
#include "envoy/data/tap/v2alpha/wrapper.pb.h"
#include "envoy/event/timer.h"
#include "envoy/network/transport_socket.h"

#include "extensions/common/tap/extension_config_base.h"
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
  void onRead(absl::string_view data) override;
  void onWrite(absl::string_view data, bool end_stream) override;

private:
  SocketTapConfigImplSharedPtr config_;
  const Network::Connection& connection_;
  std::vector<bool> statuses_;
  // TODO(mattklein123): Buffering the entire trace until socket close won't scale to
  // long lived connections or large transfers. We could emit multiple tap
  // files with bounded size, with identical connection ID to allow later
  // reassembly.
  std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper> trace_;
};

class SocketTapConfigImpl : public Extensions::Common::Tap::TapConfigBaseImpl,
                            public SocketTapConfig,
                            public std::enable_shared_from_this<SocketTapConfigImpl> {
public:
  SocketTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                      Extensions::Common::Tap::Sink* admin_streamer, Event::TimeSystem& time_system)
      : Extensions::Common::Tap::TapConfigBaseImpl(std::move(proto_config), admin_streamer),
        time_system_(time_system) {}

  // SocketTapConfig
  PerSocketTapperPtr createPerSocketTapper(const Network::Connection& connection) override {
    return std::make_unique<PerSocketTapperImpl>(shared_from_this(), connection);
  }

  Event::TimeSystem& time_system_;
};

class TapSocket : public Network::TransportSocket {
public:
  TapSocket(SocketTapConfigSharedPtr config, Network::TransportSocketPtr&& transport_socket);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  const Ssl::Connection* ssl() const override;

private:
  SocketTapConfigSharedPtr config_;
  PerSocketTapperPtr tapper_;
  Network::TransportSocketPtr transport_socket_;
};

class TapSocketFactory : public Network::TransportSocketFactory,
                         public Common::Tap::ExtensionConfigBase {
public:
  TapSocketFactory(const envoy::config::transport_socket::tap::v2alpha::Tap& proto_config,
                   Common::Tap::TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
                   Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
                   Event::Dispatcher& main_thread_dispatcher,
                   Network::TransportSocketFactoryPtr&& transport_socket_factory);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;

private:
  Network::TransportSocketFactoryPtr transport_socket_factory_;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
