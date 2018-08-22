#pragma once

#include <fstream>

#include "envoy/config/transport_socket/capture/v2alpha/capture.pb.h"
#include "envoy/data/tap/v2alpha/capture.pb.h"
#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Capture {

class CaptureSocket : public Network::TransportSocket {
public:
  CaptureSocket(const std::string& path_prefix,
                envoy::config::transport_socket::capture::v2alpha::FileSink::Format format,
                Network::TransportSocketPtr&& transport_socket);

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
  const std::string& path_prefix_;
  const envoy::config::transport_socket::capture::v2alpha::FileSink::Format format_;
  // TODO(htuch): Buffering the entire trace until socket close won't scale to
  // long lived connections or large transfers. We could emit multiple capture
  // files with bounded size, with identical connection ID to allow later
  // reassembly.
  envoy::data::tap::v2alpha::Trace trace_;
  Network::TransportSocketPtr transport_socket_;
  Network::TransportSocketCallbacks* callbacks_{};
};

class CaptureSocketFactory : public Network::TransportSocketFactory {
public:
  CaptureSocketFactory(const std::string& path_prefix,
                       envoy::config::transport_socket::capture::v2alpha::FileSink::Format format,
                       Network::TransportSocketFactoryPtr&& transport_socket_factory);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr createTransportSocket() const override;
  bool implementsSecureTransport() const override;

private:
  const std::string path_prefix_;
  const envoy::config::transport_socket::capture::v2alpha::FileSink::Format format_;
  Network::TransportSocketFactoryPtr transport_socket_factory_;
};

} // namespace Capture
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
