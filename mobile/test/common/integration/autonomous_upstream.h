#pragma once

#include "test/integration/fake_upstream.h"

namespace Envoy {

class AutonomousUpstream;

// A stream which automatically responds when the downstream request is completely read.
// However, it supports only only request: https://test.example.com/simple.txt
class AutonomousStream : public FakeStream {
public:
  AutonomousStream(FakeHttpConnection& parent, Http::ResponseEncoder& encoder,
                   AutonomousUpstream& upstream);
  ~AutonomousStream() override;

  void setEndStream(bool set) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) override;

private:
  void sendResponse() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
};

// An upstream which creates AutonomousStreams for new incoming streams.
class AutonomousHttpConnection : public FakeHttpConnection {
public:
  AutonomousHttpConnection(AutonomousUpstream& autonomous_upstream,
                           SharedConnectionWrapper& shared_connection, Http::CodecType type,
                           AutonomousUpstream& upstream);

  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder, bool) override;

private:
  AutonomousUpstream& upstream_;
  std::vector<FakeStreamPtr> streams_;
};

using AutonomousHttpConnectionPtr = std::unique_ptr<AutonomousHttpConnection>;

// An upstream which creates AutonomousHttpConnection for new incoming connections.
class AutonomousUpstream : public FakeUpstream {
public:
  AutonomousUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                     const Network::Address::InstanceConstSharedPtr& address,
                     const FakeUpstreamConfig& config)
      : FakeUpstream(std::move(transport_socket_factory), address, config) {}

  AutonomousUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                     uint32_t port, Network::Address::IpVersion version,
                     const FakeUpstreamConfig& config)
      : FakeUpstream(std::move(transport_socket_factory), port, version, config) {}

  ~AutonomousUpstream() override;
  bool
  createNetworkFilterChain(Network::Connection& connection,
                           const std::vector<Network::FilterFactoryCb>& filter_factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& listener) override;
  void createUdpListenerFilterChain(Network::UdpListenerFilterManager& listener,
                                    Network::UdpReadFilterCallbacks& callbacks) override;

private:
  std::vector<AutonomousHttpConnectionPtr> http_connections_;
  std::vector<SharedConnectionWrapperPtr> shared_connections_;
};

} // namespace Envoy
