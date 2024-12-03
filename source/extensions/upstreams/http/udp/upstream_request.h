#pragma once

#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/network/utility.h"
#include "source/common/router/upstream_request.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "quiche/common/capsule.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

// Creates a UDP socket for a UDP upstream connection. When a new UDP upstream is requested by the
// UpstreamRequest of Router, creates a UDPUpstream object and hands over the created socket to it.
class UdpConnPool : public Router::GenericConnPool {
public:
  UdpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::LoadBalancerContext* ctx)
      : host_(thread_local_cluster.loadBalancer().chooseHost(ctx)) {}

  // Creates a UDPUpstream object for a new stream.
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override;

  bool cancelAnyPendingStream() override {
    // Unlike TCP, UDP Upstreams do not have any pending streams because the upstream connection is
    // created immediately without a handshake.
    return false;
  }

  Upstream::HostDescriptionConstSharedPtr host() const override { return host_; }

  Network::SocketPtr createSocket(const Upstream::HostConstSharedPtr& host) {
    auto ret = std::make_unique<Network::SocketImpl>(
        Network::Socket::Type::Datagram, host->address(),
        /*remote_address=*/nullptr, Network::SocketCreationOptions{});
    RELEASE_ASSERT(ret->isOpen(), "Socket creation fail");
    return ret;
  }

  bool valid() const override { return host_ != nullptr; }

private:
  Upstream::HostConstSharedPtr host_;
};

// Maintains data relevant to a UDP upstream connection including the socket for the upstream.
// When a CONNECT-UDP request comes in, connects the socket to a node in the upstream cluster.
// Also, adds appropriate header entries to the CONNECT-UDP response.
class UdpUpstream : public Router::GenericUpstream,
                    public Network::UdpPacketProcessor,
                    public quiche::CapsuleParser::Visitor {
public:
  UdpUpstream(Router::UpstreamToDownstream* upstream_to_downstream, Network::SocketPtr socket,
              Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher);

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap&, bool end_stream) override;
  void encodeTrailers(const Envoy::Http::RequestTrailerMap&) override {}
  void readDisable(bool) override {}
  void resetStream() override;
  void enableTcpTunneling() override {}
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}
  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  // Network::UdpPacketProcessor
  // Handles data received from the UDP Upstream.
  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                     Buffer::RawSlice saved_cmsg) override;
  uint64_t maxDatagramSize() const override { return Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE; }
  void onDatagramsDropped(uint32_t dropped) override {
    // TODO(https://github.com/envoyproxy/envoy/issues/23564): Add statistics for CONNECT-UDP
    // upstreams.
    ENVOY_LOG_MISC(warn, "{} UDP datagrams were dropped.", dropped);
    datagrams_dropped_ += dropped;
  }
  size_t numPacketsExpectedPerEventLoop() const override {
    return Network::MAX_NUM_PACKETS_PER_EVENT_LOOP;
  }
  uint32_t numOfDroppedDatagrams() { return datagrams_dropped_; }
  const Network::IoHandle::UdpSaveCmsgConfig& saveCmsgConfig() const override {
    static const Network::IoHandle::UdpSaveCmsgConfig empty_config{};
    return empty_config;
  };

  // quiche::CapsuleParser::Visitor
  bool OnCapsule(const quiche::Capsule& capsule) override;
  void OnCapsuleParseFailure(absl::string_view error_message) override;

private:
  void onSocketReadReady();

  Router::UpstreamToDownstream* upstream_to_downstream_;
  const Network::SocketPtr socket_;
  Upstream::HostConstSharedPtr host_;
  Event::Dispatcher& dispatcher_;
  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
  quiche::CapsuleParser capsule_parser_{this};
  quiche::SimpleBufferAllocator capsule_buffer_allocator_;
  uint32_t datagrams_dropped_ = 0;
};

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
