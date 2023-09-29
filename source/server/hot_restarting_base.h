#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/server/hot_restart.pb.h"

namespace Envoy {
namespace Server {

class RpcStream : public Logger::Loggable<Logger::Id::main> {
public:
  enum class Blocking { Yes, No };

  explicit RpcStream(uint64_t base_id) : base_id_(base_id) {}
  ~RpcStream();
  void initDomainSocketAddress(sockaddr_un* address);
  sockaddr_un createDomainSocketAddress(uint64_t id, const std::string& role,
                                        const std::string& socket_path, mode_t socket_mode);
  void bindDomainSocket(uint64_t id, const std::string& role, const std::string& socket_path,
                        mode_t socket_mode);

  // Protocol description:
  //
  // In each direction between parent<-->child, a series of pairs of:
  //   A uint64 'length' (bytes in network order),
  //   followed by 'length' bytes of a serialized HotRestartMessage.
  // Each new message must start in a new sendmsg datagram, i.e. 'length' must always start at
  // byte 0. Each sendmsg datagram can be up to 4096 bytes (including 'length' if present). When
  // the serialized protobuf is longer than 4096-8 bytes, and so cannot fit in just one datagram,
  // it is delivered by a series of datagrams. In each of these continuation datagrams, the
  // protobuf data starts at byte 0.
  //
  // There is no mechanism to explicitly pair responses to requests. However, the child initiates
  // all exchanges, and blocks until a reply is received, so there is implicit pairing.
  void sendHotRestartMessage(sockaddr_un& address, const envoy::HotRestartMessage& proto);

  // Receive data, possibly enough to build one of our protocol messages.
  // If block is true, blocks until a full protocol message is available.
  // If block is false, returns nullptr if we run out of data to receive before a full protocol
  // message is available. In either case, the HotRestartingBase may end up buffering some data
  // for the next protocol message, even if the function returns a protobuf.
  std::unique_ptr<envoy::HotRestartMessage> receiveHotRestartMessage(Blocking block);
  bool replyIsExpectedType(const envoy::HotRestartMessage* proto,
                           envoy::HotRestartMessage::Reply::ReplyCase oneof_type) const;

  int domain_socket_{-1};

private:
  void getPassedFdIfPresent(envoy::HotRestartMessage* out, msghdr* message);
  std::unique_ptr<envoy::HotRestartMessage> parseProtoAndResetState();
  void initRecvBufIfNewMessage();
  // An int in [0, MaxConcurrentProcesses). As hot restarts happen, each next process gets the
  // next of 0,1,2,0,1,...
  // A HotRestartingBase's domain socket's name contains its base_id_ value, and so we can use
  // this value to determine which domain socket name to treat as our parent, and which to treat
  // as our child. (E.g. if we are 2, 1 is parent and 0 is child).
  const uint64_t base_id_;
  // State for the receiving half of the protocol.
  //
  // When filled, the size in bytes that the in-flight HotRestartMessage should be.
  // When empty, we're ready to start receiving a new message (starting with a uint64 'length').
  absl::optional<uint64_t> expected_proto_length_;
  // How much of the current in-flight message (including both the uint64 'length', plus the proto
  // itself) we have received. Once this equals expected_proto_length_ + sizeof(uint64_t), we're
  // ready to parse the HotRestartMessage. Should be set to 0 in between messages, to indicate
  // readiness for a new message.
  uint64_t cur_msg_recvd_bytes_{};
  // The first 8 bytes will always be the raw net-order bytes of the current value of
  // expected_proto_length_. The protobuf partial data starts at byte 8.
  // Should be resized to 0 in between messages, to indicate readiness for a new message.
  std::vector<uint8_t> recv_buf_;
};

/**
 * Logic shared by the implementations of both sides of the child<-->parent hot restart protocol:
 * domain socket communication, and our ad hoc RPC protocol.
 */
class HotRestartingBase : public Logger::Loggable<Logger::Id::main> {
protected:
  HotRestartingBase(uint64_t base_id)
      : main_rpc_stream_(base_id), udp_forwarding_rpc_stream_(base_id) {}

  // Returns a Gauge that tracks hot-restart generation, where every successive
  // child increments this number.
  static Stats::Gauge& hotRestartGeneration(Stats::Scope& scope);

  // A stream over a unix socket between the parent and child instances, used
  // for the child instance to request socket information and control draining
  // and shutdown of the parent.
  RpcStream main_rpc_stream_;

  // A separate channel is used for udp forwarding because udp forwarding can
  // begin while communication on the main channel is still occurring. The hot
  // restarter is single-threaded, so we don't have to worry about packets coming
  // in a jumbled order, but there are two instances of the hot restarter, the
  // parent and the child; it is possible for the child to send a udp packet
  // while the parent is sending a request on the main channel, for which it will
  // expect to receive a response (and not an unrelated udp packet). Therefore, a
  // separate channel is used to deliver udp packets, ensuring no interference
  // between the two data sources.
  RpcStream udp_forwarding_rpc_stream_;
};

} // namespace Server
} // namespace Envoy
