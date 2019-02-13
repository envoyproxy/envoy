#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/admin/v2alpha/hot_restart.pb.h"
#include "envoy/common/platform.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats_options.h"

#include "common/common/assert.h"
#include "common/stats/raw_stat_data.h"

namespace Envoy {
namespace Server {

/**
 * Logic shared by the implementations of both sides of the child<-->parent hot restart protocol:
 * domain socket communication, and our ad hoc RPC protocol.
 */
class HotRestartingBase {
protected:
  HotRestartingBase(int base_id) : base_id_(base_id) {}

  void initDomainSocketAddress(sockaddr_un* address);
  sockaddr_un createDomainSocketAddress(uint64_t id, const std::string& role);
  void bindDomainSocket(uint64_t id, const std::string& role);
  int my_domain_socket() const { return my_domain_socket_; }

  // Protocol description:
  //
  // In each direction between parent<-->child, a series of pairs of:
  //   A uint64 'length' (bytes in host order),
  //   followed by 'length' bytes of a serialized HotRestartMessage.
  // Each new message must start in a new sendmsg datagram, i.e. 'length' must always start at byte
  // 0. Each sendmsg datagram can be up to 4096 bytes (including 'length' if present). When the
  // serialized protobuf is longer than 4096-8 bytes, and so cannot fit in just one datagram, it is
  // delivered by a series of datagrams. In each of these continuation datagrams, the protobuf data
  // starts at byte 0.
  //
  // There is no mechanism to explicitly pair responses to requests. However, the child initiates
  // all exchanges, and blocks until a reply is received, so there is implicit pairing.
  void sendHotRestartMessage(sockaddr_un& address,
                             const envoy::admin::v2alpha::HotRestartMessage& proto);
  // Receive data, possibly enough to build one of our protocol messages.
  // If block is true, blocks until a full protocol message is available.
  // If block is false, returns nullptr if we run out of data to receive before a full protocol
  // message is available. In either case, the HotRestartingBase may end up buffering some data for
  // the next protocol message, even if the function returns a protobuf.
  std::unique_ptr<envoy::admin::v2alpha::HotRestartMessage> receiveHotRestartMessage(bool block);

private:
  void resizeRecvBuf(uint64_t new_size, uint64_t cur_bytes_used);
  void getPassedFdIfPresent(envoy::admin::v2alpha::HotRestartMessage* out, msghdr* message);

  const int base_id_;
  int my_domain_socket_{-1};

  const uint64_t MaxSendmsgSize = 4096;

  // State for the receiving half of the protocol.
  //
  // When filled, the size in bytes that the in-flight HotRestartMessage should be.
  // When empty, we're ready to start receiving a new message (starting with a uint64_t 'length').
  absl::optional<uint64_t> expected_message_length_{};
  // How much of the current in-flight HotRestartMessage we have received. Once this equals
  // expected_message_length_, we're ready to parse the HotRestartMessage.
  uint64_t partial_proto_bytes_{};
  // When not empty, the first 8 bytes will always be the raw bytes of the current value of
  // expected_message_length_. The protobuf partial data starts at byte 8.
  std::vector<uint8_t> recv_buf_;
  // The byte after the last occupied byte of recv_buf_, i.e. the next byte to recvmsg() into.
  uint8_t* receive_next_byte_here_{};
};

} // namespace Server
} // namespace Envoy
