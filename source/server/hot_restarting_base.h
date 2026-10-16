#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

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
  //
  // Sends block, but only for up to SEND_TIMEOUT per datagram (the socket's send timeout): the
  // peer's main thread may be busy or gone, and blocking on it forever from our own main thread
  // is a deadlock. Returns false without asserting when a datagram could not be sent within that
  // time, or on ECONNREFUSED with `allow_failure`; anything else is fatal.
  bool sendHotRestartMessage(sockaddr_un& address, const envoy::HotRestartMessage& proto,
                             bool allow_failure = false);

  enum class SendResult {
    Sent,
    // The peer's socket is gone (ECONNREFUSED): the peer process has exited.
    ConnectionRefused,
    // The peer's socket is full and was not drained within SEND_TIMEOUT: the peer is not reading.
    TimedOut,
  };
  // sendHotRestartMessage(address, proto, /*allow_failure=*/true), reporting why a send failed.
  SendResult trySendHotRestartMessage(sockaddr_un& address, const envoy::HotRestartMessage& proto);

  // Receive data, possibly enough to build one of our protocol messages.
  // If block is true, blocks until a full protocol message is available or the socket's receive
  // timeout (RECEIVE_TIMEOUT_SLICE) elapses without data, returning nullptr in the latter case so
  // the caller can do other work (e.g. service forwarded UDP packets) and call again.
  // If block is false, returns nullptr if we run out of data to receive before a full protocol
  // message is available. In either case, the HotRestartingBase may end up buffering some data
  // for the next protocol message, even if the function returns a protobuf.
  std::unique_ptr<envoy::HotRestartMessage> receiveHotRestartMessage(Blocking block);

  // Non-blocking send path, used by the parent for forwarded UDP packets. The message's datagrams
  // are appended (all or none) to a bounded queue that flushSendQueue() drains with non-blocking
  // sendmsg() calls, so the caller's thread never waits on the peer. Returns false and drops the
  // message when the queue is full (MAX_SEND_QUEUE_BYTES). All queued messages go to `address`.
  bool queueHotRestartMessage(const sockaddr_un& address, const envoy::HotRestartMessage& proto);
  // Sends queued datagrams in order until the queue is empty (returns true) or the socket would
  // block (returns false; call again once it may have drained). A send failure that is not
  // transient, e.g. ECONNREFUSED because the peer is gone, drops the whole queue: nothing on this
  // path is worth stalling or crashing the process for.
  bool flushSendQueue();
  uint64_t sendQueueBytes() const { return send_queue_bytes_; }
  uint64_t sendQueueDatagrams() const { return send_queue_.size(); }
  // Datagrams the last flushSendQueue() call dropped on a fatal send error (0 when none).
  uint64_t lastFlushDropped() const { return last_flush_dropped_; }

  // Bound on a single blocking sendmsg() on this stream's socket (its socket send timeout).
  static constexpr std::chrono::milliseconds SEND_TIMEOUT{1000};
  // Bound on a single blocking recvmsg() (the socket receive timeout); see
  // receiveHotRestartMessage().
  static constexpr std::chrono::milliseconds RECEIVE_TIMEOUT_SLICE{10};
  // Bound on the bytes queued by queueHotRestartMessage() awaiting flushSendQueue().
  static constexpr uint64_t MAX_SEND_QUEUE_BYTES = 1024 * 1024;
  bool replyIsExpectedType(const envoy::HotRestartMessage* proto,
                           envoy::HotRestartMessage::Reply::ReplyCase oneof_type) const;

  int domain_socket_{-1};

private:
  SendResult sendHotRestartMessageImpl(sockaddr_un& address, const envoy::HotRestartMessage& proto,
                                       bool allow_failure);
  // Fills `datagrams` with the wire form of `proto`: a uint64 'length' followed by the serialized
  // message, split at MaxSendmsgSize as the protocol requires.
  static void serializeToDatagrams(const envoy::HotRestartMessage& proto,
                                   std::vector<std::vector<uint8_t>>& datagrams);
  void setSocketTimeouts();
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
  std::optional<uint64_t> expected_proto_length_;
  // How much of the current in-flight message (including both the uint64 'length', plus the proto
  // itself) we have received. Once this equals expected_proto_length_ + sizeof(uint64_t), we're
  // ready to parse the HotRestartMessage. Should be set to 0 in between messages, to indicate
  // readiness for a new message.
  uint64_t cur_msg_recvd_bytes_{};
  // The first 8 bytes will always be the raw net-order bytes of the current value of
  // expected_proto_length_. The protobuf partial data starts at byte 8.
  // Should be resized to 0 in between messages, to indicate readiness for a new message.
  std::vector<uint8_t> recv_buf_;
  // Datagrams awaiting a non-blocking send by flushSendQueue(), in order; see
  // queueHotRestartMessage().
  std::deque<std::vector<uint8_t>> send_queue_;
  uint64_t send_queue_bytes_{};
  uint64_t last_flush_dropped_{};
  sockaddr_un send_queue_address_{};
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
